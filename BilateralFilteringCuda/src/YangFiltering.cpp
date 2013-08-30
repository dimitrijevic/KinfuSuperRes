#include "YangFiltering.h"

#include "GpuDepthMap.hpp"
#include "AmCudaUtil.h"

#include <opencv2/highgui/highgui.hpp>

extern void squareDiff( GpuDepthMap<float> const& d_fDep, float d,
                        GpuDepthMap<float>      & d_C2  , float truncAt );
extern void minMaskedCopy( GpuDepthMap<float> const& C,
                           GpuDepthMap<float> const& Cprev ,
                           GpuDepthMap<float>      & minC  ,
                           GpuDepthMap<float>      & minCm1,
                           GpuDepthMap<float>      & minCp1,
                           GpuDepthMap<float>      & minDs ,
                           const float d );
extern void subpixelRefine( GpuDepthMap<float> const& minC  ,
                            GpuDepthMap<float> const& minCm1,
                            GpuDepthMap<float> const& minCp1,
                            GpuDepthMap<float> const& minDs ,
                            GpuDepthMap<float>      & fDep_next );

template <typename T>
extern T getMax( GpuDepthMap<T> const& img );

int YangFiltering::run( cv::Mat const& dep16, const cv::Mat &img8, cv::Mat &fDep, const YangFilteringRunParams params, std::string depPath )
{
    const float ETA_L_2 = params.ETA * params.L * params.ETA * params.L;
    StopWatchInterface* kernel_timer = NULL;
    sdkCreateTimer( &kernel_timer );
    updateGaussian( params.spatial_sigma, params.kernel_range );

    std::cout << "running yang " << params.spatial_sigma << " " << params.range_sigma << " "  << params.kernel_range << std::endl;


    /// parse input
    // fDep // scale to 0.f .. MAXRES
    if ( dep16.type() == CV_16UC1 ) { dep16.convertTo( fDep, CV_32FC1 ); std::cerr << "YangFiltering::run(): warning, converting from 16UC1" << std::endl; }
    else                            { dep16.copyTo   ( fDep ); }

    float *fDepArr = NULL;
    toContinuousFloat( fDep, fDepArr );
    // move to device
    //GpuDepthMap<float> d_fDep;
    d_fDep.Create( DEPTH_MAP_TYPE_FLOAT, fDep.cols, fDep.rows );
    d_fDep.CopyDataIn( fDepArr );

    // guide
    unsigned *guideArr = NULL;
    cv2Continuous8UC4( img8, guideArr, img8.cols, img8.rows, 1.f );
    //GpuImage d_guide;
    d_guide.Create( IMAGE_TYPE_XRGB32, img8.cols, img8.rows );
    d_guide.CopyDataIn( guideArr );

    // device temporary memory
    //GpuDepthMap<float> d_truncC2;
    d_truncC2.Create( DEPTH_MAP_TYPE_FLOAT, fDep.cols, fDep.rows );
    //GpuDepthMap<float> d_filteredTruncC2s[2];
    d_filteredTruncC2s[0].Create( DEPTH_MAP_TYPE_FLOAT, fDep.cols, fDep.rows );
    d_filteredTruncC2s[1].Create( DEPTH_MAP_TYPE_FLOAT, fDep.cols, fDep.rows );
    uchar fc2_id = 0;

    //GpuDepthMap<float> d_crossFilterTemp;
    d_crossFilterTemp.Create( DEPTH_MAP_TYPE_FLOAT, fDep.cols, fDep.rows );

    //GpuDepthMap<float> d_minDs;
    d_minDs.Create( DEPTH_MAP_TYPE_FLOAT, fDep.cols, fDep.rows );
    //GpuDepthMap<float> d_minC;
    d_minC.Create( DEPTH_MAP_TYPE_FLOAT, fDep.cols, fDep.rows );
    //GpuDepthMap<float> d_minCm1;
    d_minCm1.Create( DEPTH_MAP_TYPE_FLOAT, fDep.cols, fDep.rows );
    //GpuDepthMap<float> d_minCp1;
    d_minCp1.Create( DEPTH_MAP_TYPE_FLOAT, fDep.cols, fDep.rows );

    // filter cost slice
    const cudaExtent imageSizeCudaExtent = make_cudaExtent( d_truncC2.GetWidth(), d_truncC2.GetHeight(), 1 );

    // size
    int len        = d_fDep.GetWidth() * d_fDep.GetHeight();
    // allocate
    float *tmpFDep = new float[ len ];

    for ( int it = 0; it < params.yang_iterations; ++it )
    {
        std::cout << "Yang_iteration " << it << std::endl;
        // set costs to maximum
        runSetKernel2D<float>(   d_minC.Get(), params.MAXRES*params.MAXRES,   d_minC.GetWidth(),   d_minC.GetHeight() );
        runSetKernel2D<float>( d_minCm1.Get(), params.MAXRES*params.MAXRES, d_minCm1.GetWidth(), d_minCm1.GetHeight() );
        runSetKernel2D<float>( d_minCp1.Get(), params.MAXRES*params.MAXRES, d_minCp1.GetWidth(), d_minCp1.GetHeight() );

        // minmax
        float mx = 0.f, mn = FLT_MAX;
        {
            // device to host
            d_fDep.CopyDataOut( tmpFDep );
            // select minmax
            for ( int i = 0; i < len; ++i )
            {
                // max
                if ( tmpFDep[i] > mx ) mx = tmpFDep[i];
                // min
                if ( tmpFDep[i] < mn ) mn = tmpFDep[i];
            }
        }

        const float loop_stop = std::min( mx + params.L * params.ETA + 1.f, params.MAXRES );
        for ( float d = std::max( 0.f, mn - params.L * params.ETA); d < loop_stop; d += params.d_step )
        {
            // debug
            std::cout << "d: " << d << "/" << mx + params.L + 1<< " of it(" << it << ")" << std::endl;

            // calculate truncated cost
            squareDiff( /*         in: */ d_fDep,
                        /* curr depth: */ d,
                        /*        out: */ d_truncC2,
                        /*    truncAt: */ ETA_L_2 );


            crossBilateralFilterF<float>( /* out: */ d_filteredTruncC2s[fc2_id].Get(), d_filteredTruncC2s[fc2_id].GetPitch(),
                                          /* in:  */ d_truncC2.Get(), d_crossFilterTemp.Get(), d_truncC2.GetPitch(),
                                          d_guide.Get(), d_guide.GetPitch(),
                                          imageSizeCudaExtent,
                                          params.range_sigma, params.kernel_range, params.cross_iterations, params.fill_mode,
                                          kernel_timer );

            // track minimums
            minMaskedCopy( /* curr C: */ d_filteredTruncC2s[ fc2_id       ],
                           /* prev C: */ d_filteredTruncC2s[ (fc2_id+1)%2 ],
                                         d_minC, d_minCm1, d_minCp1, d_minDs,
                                         d );
            // swap filter output targets
            fc2_id = (fc2_id+1) % 2;
        }

        // refine minDs based on C(d_min), C(d_min-1), C(d_min+1)
        subpixelRefine( d_minC, d_minCm1, d_minCp1, d_minDs, d_fDep );

        // copy out
        d_fDep.CopyDataOut( fDepArr );
        fromContinuousFloat( fDepArr, fDep );

        // out
        cv::Mat dep_out;
        fDep.convertTo( dep_out, CV_16UC1 );
        char title[255];
        sprintf( title, (depPath+"/iteration16_%d.png").c_str(), it );
        // write "iteration"
        std::vector<int> imwrite_params;
        imwrite_params.push_back( 16 /*cv::IMWRITE_PNG_COMPRESSION */ );
        imwrite_params.push_back( 0 );
        cv::imwrite( title, dep_out, imwrite_params );
        fDep.convertTo( dep_out, CV_8UC1, 255.f / 10001.f );
        sprintf( title, (depPath+"/iteration8_%d.png").c_str(), it );
        cv::imwrite( title, dep_out, imwrite_params );
    }


    // copy out
    d_fDep.CopyDataOut( fDepArr );
    fromContinuousFloat( fDepArr, fDep );

    // cleanup
    SAFE_DELETE_ARRAY( fDepArr );
    SAFE_DELETE_ARRAY( guideArr );
    SAFE_DELETE_ARRAY( tmpFDep );

    return 0;
}

