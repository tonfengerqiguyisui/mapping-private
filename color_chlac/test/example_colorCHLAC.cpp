#include <ros/ros.h>
#include <pcl/point_types.h>
#include <pcl/features/feature.h>
#include <pcl/io/pcd_io.h>
#include "color_chlac/ColorCHLAC.hpp"
#include "color_chlac/ColorVoxel.hpp"

using namespace pcl;

template <typename T>
bool readPoints( const char *name, T& cloud_object_cluster ){
  if (pcl::io::loadPCDFile (name, cloud_object_cluster) == -1){
    ROS_ERROR ("Couldn't read file %s",name);
    return (-1);
  }
  ROS_INFO ("Loaded %d data points from %s with the following fields: %s", (int)(cloud_object_cluster.width * cloud_object_cluster.height), name, pcl::getFieldsList (cloud_object_cluster).c_str ());
  return(1);
}

bool writeColorCHLAC(const char *name, pcl::PointCloud<pcl::PointXYZRGB> cloud_object_cluster ){
  // compute voxel
  ColorVoxel voxel;
  voxel.setVoxelSize( 0.05 );
  //new
  //  voxel.points2voxel( cloud_object_cluster, TRIGONOMETRIC );
  //old
  voxel.points2voxel( cloud_object_cluster, SIMPLE_REVERSE );
  ColorVoxel voxel_bin;
  voxel_bin = voxel;
  voxel_bin.binarize( 127, 127, 127 );

  // compute colorCHLAC
  std::vector<float> colorCHLAC;
  ColorCHLAC::extractColorCHLAC( colorCHLAC, voxel );
  colorCHLAC.resize( DIM_COLOR_1_3+DIM_COLOR_BIN_1_3 );
  std::vector<float> tmp;
  ColorCHLAC::extractColorCHLAC_bin( tmp, voxel_bin );
  for(int t=0;t<DIM_COLOR_BIN_1_3;t++)
    colorCHLAC[ t+DIM_COLOR_1_3 ] = tmp[ t ];

  // save colorCHLAC
  FILE *fp = fopen( name, "w" );
  fprintf(fp,"# .PCD v.? - Point Cloud Data file format\n");
  fprintf(fp,"FIELDS colorCHLAC\n");
  fprintf(fp,"SIZE 4\n");
  fprintf(fp,"TYPE F\n");
  fprintf(fp,"COUNT %d\n",DIM_COLOR_BIN_1_3+DIM_COLOR_1_3);
  fprintf(fp,"WIDTH 1\n");
  fprintf(fp,"HEIGHT 1\n");
  fprintf(fp,"POINTS 1\n");
  fprintf(fp,"DATA ascii\n");
  for(int t=0;t<DIM_COLOR_BIN_1_3+DIM_COLOR_1_3;t++)
    fprintf(fp,"%f ",colorCHLAC[ t ]);
  fprintf(fp,"\n");
  fclose(fp);
  ROS_INFO("ColorCHLAC signatures written to %s", name);
  return 1;
}

int main( int argc, char** argv ){
  if( argc != 2 ){
    ROS_ERROR ("Need one parameter! Syntax is: %s {input_pointcloud_filename.pcd}\n", argv[0]);
    return(-1);
  }
  char filename[ 300 ];
  int length;

  //* write - ColorCHLAC -
  pcl::PointCloud<PointXYZRGB> cloud_object_cluster2;
  readPoints( argv[1], cloud_object_cluster2 );
  length = strlen( argv[1] );
  argv[1][ length-4 ] = '\0';
  sprintf(filename,"%s_colorCHLAC.pcd",argv[1]);
  writeColorCHLAC( filename, cloud_object_cluster2 );

  return(0);
}