#include "tsdf_viewer.h"

#include "DepthViewer3D.h"
#include "MeshRayCaster.h"
#include "PolyMeshViewer.h"
#include "UpScaling.h"

#include "BilateralFilterCuda.hpp"
#include "ViewPointMapperCuda.h"
#include "YangFilteringWrapper.h"

#include "my_screenshot_manager.h"

#include "MaUtil.h"

#include <pcl/io/vtk_lib_io.h>
#include <pcl/console/parse.h>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <eigen3/Eigen/Dense>
#include <boost/filesystem.hpp>

#include <map>
#include <string>
#include <iostream>

// --in ~/rec/troll_recordings/short_prism_kinect_20130816_1206/short_20130816_1327_nomap
// --in /home/amonszpart/rec/testing/ram_20130818_1209_lf_200/cloud_mesh.ply
// --in ~/workspace/rec/testing/ram_20130818_1209_lf_200/cloud_mesh.ply

// global state
struct MyPlayer
{
        bool exit;      // has to exit
        bool changed;   // has to redraw

        void *weak_cloud_viewer_ptr;

        MyPlayer()
            : exit( false ), changed( false )
        {}

        Eigen::Affine3f      & Pose()       { changed = true; return pose; }
        Eigen::Affine3f const& Pose() const {                 return pose; }

    protected:
        Eigen::Affine3f pose;

} g_myPlayer;

// all viewers keyboard callback
void keyboard_callback( const pcl::visualization::KeyboardEvent &e, void *cookie )
{
    MyPlayer* pMyPlayer = reinterpret_cast<MyPlayer*>( cookie );

    int key = e.getKeyCode ();

    if ( e.keyUp () )
    {
        switch ( key )
        {
            case 27:
                pMyPlayer->exit = true;
                break;
            case 82:
            case 'a':
                pMyPlayer->Pose().translation().x() -= 0.1f;
                break;
            case 'd':
            case 83:
                pMyPlayer->Pose().translation().x() += 0.1f;
                break;
            case 's':
            case 84:
                pMyPlayer->Pose().translation().y() -= 0.1f;
                break;
            case 'w':
            case 81:
                pMyPlayer->Pose().translation().y() += 0.1f;
                break;
            case 'e':
                pMyPlayer->Pose().translation().z() += 0.1f;
                break;
            case 'c':
                pMyPlayer->Pose().translation().z() -= 0.1f;
                break;

            default:
                break;
        }
    }
    std::cout << (int)key << std::endl;
}

// cloudviewer mouse callback
void mouse_callback (const pcl::visualization::MouseEvent& mouse_event, void* cookie)
{
    // player pointer
    MyPlayer* pMyPlayer = reinterpret_cast<MyPlayer*>( cookie );

    // left button release
    if ( mouse_event.getType()   == pcl::visualization::MouseEvent::MouseButtonRelease &&
         mouse_event.getButton() == pcl::visualization::MouseEvent::LeftButton            )
    {
        // debug
        std::cout << g_myPlayer.Pose().linear() << g_myPlayer.Pose().translation() << std::endl;

        // read
        Eigen::Affine3f tmp_pose = reinterpret_cast<pcl::visualization::PCLVisualizer*>(pMyPlayer->weak_cloud_viewer_ptr)->getViewerPose();

        // modify
        //tmp_pose.linear() = tmp_pose.linear().inverse();

        // write
        g_myPlayer.Pose() = tmp_pose; // reinterpret_cast<pcl::visualization::PCLVisualizer*>(pMyPlayer->weak_cloud_viewer_ptr)->getViewerPose();

        // debug
        std::cout << g_myPlayer.Pose().linear() << g_myPlayer.Pose().translation() << std::endl;
    }
}

// CLI usage
void printUsage()
{
    std::cout << "Usage:\n\tTSDFVis --in cloud.dat\n" << std::endl;
    std::cout << "\tYang usage: --yangd dir --dep depName --img imgName"
              << " [--spatial_sigma x]"
              << " [--range_sigma x]"
              << " [--kernel_range x]"
              << " [--cross_iterations x]"
              << " [--iter yangIterationCount]"
              << std::endl;
}

// main
int main( int argc, char** argv )
{
    // test intrinsics
    Eigen::Matrix3f intrinsics;
    intrinsics << 521.7401 * 2.f, 0             , 323.4402 * 2.f,
                  0             , 522.1379 * 2.f, 258.1387 * 2.f,
                  0             , 0             , 1             ;

    //// YANG /////////////////////////////////////////////////////////////////////////////////////////////////////////
    {
        bool canDoYang = true;

        // input dir
        std::string yangDir;
        canDoYang &= pcl::console::parse_argument (argc, argv, "--yangd", yangDir) >= 0;
        if ( canDoYang )
        {
            // depth
            std::string depName;
            canDoYang &= pcl::console::parse_argument (argc, argv, "--dep", depName) >= 0;

            // image
            std::string imgName;
            canDoYang &= pcl::console::parse_argument (argc, argv, "--img", imgName) >= 0;

            if ( canDoYang )
            {
                if ( pcl::console::find_switch( argc, argv, "--brute-force") > 0 )
                {
                    am::bruteRun( yangDir + "/" + depName, yangDir + "/" + imgName );
                    return EXIT_SUCCESS;
                }
            }

            YangFilteringRunParams runParams;

            pcl::console::parse_argument( argc, argv, "--spatial_sigma"   , runParams.spatial_sigma    );
            pcl::console::parse_argument( argc, argv, "--range_sigma"     , runParams.range_sigma      );
            pcl::console::parse_argument( argc, argv, "--kernel_range"    , runParams.kernel_range     );
            pcl::console::parse_argument( argc, argv, "--cross_iterations", runParams.cross_iterations );
            pcl::console::parse_argument( argc, argv, "--iter"            , runParams.yang_iterations  );
            if ( runParams.yang_iterations <= 0 ) runParams.yang_iterations = 3;
            std::cout << "Running for " << runParams.yang_iterations << std::endl;
            std::cout << "with: " << runParams.spatial_sigma << " " << runParams.range_sigma << " " << runParams.kernel_range << std::endl;

            // error check
            if ( !canDoYang )
            {
                printUsage();
                return EXIT_FAILURE;
            }

            // run
            am::runYang( yangDir + "/" + depName, yangDir + "/" + imgName, runParams );
            return EXIT_SUCCESS;
        }
        // else TSDF or PLY
    }

    //// TSDF or PLY //////////////////////////////////////////////////////////////////////////////////////////////////

    std::map<std::string, cv::Mat> mats;

    // parse input
    std::string inputFilePath;
    if (pcl::console::parse_argument (argc, argv, "--in", inputFilePath) < 0 )
    {
        printUsage();
        return 1;
    }

    // flag yes, if PLY input
    bool ply_no_tsdf = false;
    if ( boost::filesystem::extension(inputFilePath) == ".ply")
    {
        std::cout << "ext: " << boost::filesystem::extension( inputFilePath ) << std::endl;
        ply_no_tsdf = true;
    }

    int img_id = 50;
    pcl::console::parse_argument (argc, argv, "--img_id", img_id );
    std::cout << "Running for img_id " << img_id << std::endl;

        // read DEPTH
    cv::Mat dep16, large_dep16;
    {
        boost::filesystem::path dep_path = boost::filesystem::path(inputFilePath).parent_path()
                                           / std::string("poses")
                                           / (std::string("d") + boost::lexical_cast<std::string> (img_id) + std::string(".png"));
        dep16 = cv::imread( dep_path.c_str(), -1 );
        cv::resize( dep16, large_dep16, dep16.size() * 2, 0, 0, CV_INTER_NN );
    }

    // read RGB
    cv::Mat rgb8, rgb8_960;
    {
        boost::filesystem::path rgb8_path = boost::filesystem::path(inputFilePath).parent_path()
                                            / std::string("poses")
                                            / (boost::lexical_cast<std::string> (std::max(0,img_id-1)) + std::string(".png"));
        rgb8 = cv::imread( rgb8_path.c_str(), -1 );

        cv::Mat large_rgb8;
        cv::resize( rgb8, large_rgb8, large_dep16.size(), 0, 0, CV_INTER_NN );
        ViewPointMapperCuda::undistortRgb( rgb8_960, large_rgb8, am::viewpoint_mapping::INTR_RGB_1280_960, am::viewpoint_mapping::INTR_RGB_1280_960 );
    }

    // read poses
    std::map<int,Eigen::Affine3f> poses;
    {
        boost::filesystem::path poses_path = boost::filesystem::path(inputFilePath).parent_path()
                                             / std::string("poses")
                                             / "poses.txt";

        am::MyScreenshotManager::readPoses( poses_path.string(), poses );
    }

    am::UpScaling upScaling( intrinsics );
    upScaling.run( inputFilePath, poses[img_id], rgb8_960, img_id, -1, -1, argc, argv );
    return 0;
    // process mesh
    cv::Mat rcDepth16;
    pcl::PolygonMesh::Ptr enhancedMeshPtr;
    am::MeshRayCaster mrc( intrinsics );
    if ( 0 )
    {
        pcl::PolygonMeshPtr meshPtr( new pcl::PolygonMesh );
        pcl::io::loadPolygonFile( inputFilePath, *meshPtr );
        am::MeshRayCaster mrc( intrinsics );
        mrc.run( rcDepth16, meshPtr, poses[img_id] );

        // show output
        cv::imshow( "rcDepth16", rcDepth16 );
        cv::Mat rcDepth8;
        rcDepth16.convertTo( rcDepth8, CV_8UC1, 255.f / 10001.f );
        cv::imshow( "rcDepth8", rcDepth8 );

        // show overlay
        {
            std::vector<cv::Mat> rc8Vec = { rcDepth8, rcDepth8, rcDepth8 };
            cv::Mat rc8C3;
            cv::merge( rc8Vec.data(), 3, rc8C3 );
            std::cout << "merge ok" << std::endl;

            cv::Mat overlay;
            cv::addWeighted( rc8C3, 0.95f, rgb8_960, 0.05f, 1.0, overlay );
            cv::imshow( "overlay", overlay );
            cv::waitKey();
        }

        // show 3D
        {
            //am::DepthViewer3D depthViewer;
            //depthViewer.showMats( rcDepth16, rgb8_960, img_id, poses, intrinsics );
        }
    }
    return 0;

    // apply pose
    {
        g_myPlayer.Pose() = poses[ img_id ];
    }

    // mats to 3D
    if ( 0 )
    {
        am::DepthViewer3D depthViewer;
        depthViewer.showMats( large_dep16, rgb8_960, img_id, poses, intrinsics );
        //return 0;
    }

    // Load TSDF or MESH
    am::TSDFViewer *tsdfViewer = new am::TSDFViewer( ply_no_tsdf );
    {
        // mouse callback, prepare myplayer global state
        tsdfViewer->getCloudViewer()->registerMouseCallback( mouse_callback, (void*)&g_myPlayer );
        g_myPlayer.weak_cloud_viewer_ptr = tsdfViewer->getCloudViewer().get();

        if ( ply_no_tsdf )
        {
            // init pointer
            tsdfViewer->MeshPtr() = pcl::PolygonMesh::Ptr( new pcl::PolygonMesh() );
            // load mesh
            pcl::io::loadPolygonFile( inputFilePath, *tsdfViewer->MeshPtr() );
            tsdfViewer->setViewerPose( *tsdfViewer->getCloudViewer(), poses[img_id] );
        }
        else
        {
            // load tsdf
            tsdfViewer->loadTsdfFromFile( inputFilePath, true );
            // register keyboard callbacks
            tsdfViewer->getRayViewer()->registerKeyboardCallback (keyboard_callback, (void*)&g_myPlayer);
            tsdfViewer->getDepthViewer()->registerKeyboardCallback (keyboard_callback, (void*)&g_myPlayer);
            // check, if mesh is valid
            tsdfViewer->dumpMesh();
        }
    }




    std::vector<float> zBuffer;
    int w, h;
    while ( (!g_myPlayer.exit) && (!tsdfViewer->getCloudViewer()->wasStopped()) )
    {
        if ( g_myPlayer.changed )
        {
            if ( !ply_no_tsdf )
            {
                // raycast
                tsdfViewer->showGeneratedRayImage( tsdfViewer->kinfuVolume_ptr_, g_myPlayer.Pose() );
                // tsdf depth
                tsdfViewer->showGeneratedDepth   ( tsdfViewer->kinfuVolume_ptr_, g_myPlayer.Pose() );

                // point cloud
                //tsdfViewer->toCloud( myPlayer.Pose(), tsdfViewer->CloudPtr() );
                //tsdfViewer->showCloud(  myPlayer.Pose(), tsdfViewer->CloudPtr() );
                // range image
                //tsdfViewer->renderRangeImage( tsdfViewer->CloudPtr(), myPlayer.Pose() );
            }

            // show mesh
            tsdfViewer->showMesh( g_myPlayer.Pose(), tsdfViewer->MeshPtr() );
            // set pose
            //tsdfViewer->setViewerPose( *tsdfViewer->getCloudViewer(), g_myPlayer.Pose() );
            //tsdfViewer->getCloudViewer()->setCameraParameters( intr_m3f, myPlayer.Pose().matrix() );

            // dump zbuffer
            tsdfViewer->fetchVtkZBuffer( zBuffer, w, h );

            mrc.enhanceMesh( enhancedMeshPtr, large_dep16, tsdfViewer->MeshPtr(), poses[img_id] );
            tsdfViewer->MeshPtr() = enhancedMeshPtr;

            tsdfViewer->showMesh( g_myPlayer.Pose(), tsdfViewer->MeshPtr() );

            /*mats["large_dep16"] = large_dep16;
            mats["rgb8"]        = rgb8;
            cv::Mat zBufMat;
            processZBuffer( zBuffer, w, h, mats, zBufMat );*/

            g_myPlayer.changed = false;
        }
        // update
        tsdfViewer->spinOnce(10);

        // exit after one iteration
        //g_myPlayer.exit = true;
    }

    cv::imshow( "large_dep16", large_dep16 );

    // wait for exit
    {
        char c = 0;
        while ( (c = cv::waitKey()) != 27 ) ;
    }

    // dump latest tsdf depth
    if ( !ply_no_tsdf )
    {
        // get depth
        auto vshort = tsdfViewer->getLatestDepth();
        cv::Mat dep( 480, 640, CV_16UC1, vshort.data() );
        //util::writePNG( "dep224.png", dep );
    }

    // cleanup
    {
        if ( tsdfViewer ) { delete tsdfViewer; tsdfViewer = NULL; }
        std::cout << "Tsdf_vis finished ok" << std::endl;
    }

} // end main

#if 0
    Eigen::Affine3f pose;
    pose.linear() <<  0.999154,  -0.0404336, -0.00822448,
            0.0344457,    0.927101,   -0.373241,
            0.0227151,    0.372638,    0.927706;
    pose.translation() << 1.63002,
            1.46289,
            0.227635;

    // 224
    //Eigen::Affine3f pose;
    myPlayer.Pose().translation() << 1.67395, 1.69805, -0.337846;

    myPlayer.Pose().linear() <<
                     0.954062,  0.102966, -0.281364,
                    -0.16198,  0.967283, -0.195268,
                    0.252052,  0.231873,  0.939525;
#endif