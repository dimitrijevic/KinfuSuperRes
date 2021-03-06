#ifndef KINFU_UTIL_H
#define KINFU_UTIL_H

#include "kinfu_pcl_headers.h"

using namespace std;
using namespace pcl;
using namespace pcl::gpu;
using namespace Eigen;
namespace pc = pcl::console;

namespace pcl
{
    namespace device
    {
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Types
        typedef unsigned short ushort;
        typedef DeviceArray2D<float> MapArr;
        typedef DeviceArray2D<ushort> DepthMap;
        //typedef float4 PointType;

        //TSDF fixed point divisor (if old format is enabled)
        const int DIVISOR = 32767;     // SHRT_MAX;

        //Should be multiple of 32
        enum { VOLUME_X = 512, VOLUME_Y = 512, VOLUME_Z = 512 };
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

vector<string>
getPcdFilesInDir(const string& directory);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Eigen::Affine3f
getViewerPose (visualization::PCLVisualizer& viewer);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void
setViewerPose (visualization::PCLVisualizer& viewer, const Eigen::Affine3f& viewer_pose);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename CloudT> void
writeCloudFile (int format, const CloudT& cloud, std::string fileName = "cloud");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void
writePolygonMeshFile (int format, const pcl::PolygonMesh& mesh, string fileName);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
boost::shared_ptr<pcl::PolygonMesh> convertToMesh( const DeviceArray<PointXYZ>& triangles );

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename MergedT, typename PointT>
typename PointCloud<MergedT>::Ptr merge(const PointCloud<PointT>& points, const PointCloud<RGB>& colors)
{
    typename PointCloud<MergedT>::Ptr merged_ptr(new PointCloud<MergedT>());

    pcl::copyPointCloud (points, *merged_ptr);
    for (size_t i = 0; i < colors.size (); ++i)
        merged_ptr->points[i].rgba = colors.points[i].rgba;

    return merged_ptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename CloudPtr> void
writeCloudFile (int format, const CloudPtr& cloud_ptr, std::string fileName )
{
    if (format == nsKinFuApp::PCD_BIN)
    {
        cout << "Saving point cloud to '" + fileName + "_bin.pcd' (binary)... " << flush;
        pcl::io::savePCDFile (fileName + "_bin.pcd", *cloud_ptr, true);
    }
    else
        if (format == nsKinFuApp::PCD_ASCII)
        {
            cout << "Saving point cloud to '" + fileName + ".pcd' (ASCII)... " << flush;
            pcl::io::savePCDFile (fileName + ".pcd", *cloud_ptr, false );
        }
        else   /* if (format == KinFuApp::PLY) */
        {
            cout << "Saving point cloud to '" + fileName + ".ply' (ASCII)... " << flush;
            pcl::io::savePLYFileASCII (fileName + ".ply", *cloud_ptr );

        }
    cout << "Done" << endl;
}

#endif // KINFU_UTIL_H
