#include <sys/time.h>
#include <ros/ros.h>
#include <pcl/point_types.h>
#include <pcl/features/feature.h>
#include <pcl/io/pcd_io.h>
#include "pcl/features/normal_3d.h"
#include "pcl/filters/voxel_grid.h"
#include "pcl/filters/passthrough.h"
#include "pcl/kdtree/kdtree.h"
//#include "pcl/kdtree/kdtree_ann.h"
#include "pcl/kdtree/organized_data.h"
#include "color_chlac/color_chlac.h"
#include "pcl/features/rsd.h"
#include <algorithm>

using namespace pcl;

//* GRSD type
#define NR_CLASS 5 
#define NOISE 0 
#define PLANE 1 
#define CYLINDER 2
#define CIRCLE 3  
#define EDGE 4 
#define EMPTY 5 

//* const variables
const double min_radius_plane_ = 0.066;
const double min_radius_noise_ = 0.030, max_radius_noise_ = 0.050;
const double max_min_radius_diff_ = 0.01;
const double min_radius_edge_ = 0.030;
const double downsample_leaf = 0.01; // 1cm voxel size by default

//-----------
//* time
double t1,t2;
double my_clock()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + (double)tv.tv_usec*1e-6;
}

//-----------
//* read
template <typename T>
bool readPoints( const char *name, pcl::PointCloud<T>& cloud ){
  if (pcl::io::loadPCDFile (name, cloud) == -1){
    ROS_ERROR ("Couldn't read file %s",name);
    return (-1);
  }
  ROS_INFO ("Loaded %d data points from %s with the following fields: %s", (int)(cloud.width * cloud.height), name, pcl::getFieldsList (cloud).c_str ());
  return(1);
}

//-----------
//* write
void writeFeature(const char *name, std::vector<float> &feature){
  const int feature_size = feature.size();
  FILE *fp = fopen( name, "w" );
  fprintf(fp,"# .PCD v.7 - Point Cloud Data file format\n");
  fprintf(fp,"FIELDS vfh\n");
  fprintf(fp,"SIZE 4\n");
  fprintf(fp,"TYPE F\n");
  fprintf(fp,"COUNT %d\n",feature_size);
  fprintf(fp,"WIDTH 1\n");
  fprintf(fp,"HEIGHT 1\n");
  fprintf(fp,"POINTS 1\n");
  fprintf(fp,"DATA ascii\n");
  for(int t=0;t<feature_size;t++)
    fprintf(fp,"%f ",feature[ t ]);
  fprintf(fp,"\n");
  fclose(fp);
}

//------------------
//* compute normals
template <typename T1, typename T2>
void computeNormal( pcl::PointCloud<T1> input_cloud, pcl::PointCloud<T2>& output_cloud ){
  int k_ = 10;
  if ((int)input_cloud.points.size () < k_){
    ROS_WARN ("Filtering returned %d points! Continuing.", (int)input_cloud.points.size ());
    //pcl::PointCloud<pcl::PointXYZRGBNormal> cloud_temp;
    //pcl::concatenateFields <Point, pcl::Normal, pcl::PointXYZRGBNormal> (cloud_filtered, cloud_normals, cloud_temp);
    //pcl::io::savePCDFile ("test.pcd", cloud, false);
  }
  pcl::NormalEstimation<T1, T2> n3d_;
  n3d_.setKSearch (k_);
  n3d_.setSearchMethod ( boost::make_shared<pcl::KdTreeFLANN<T1> > () );
  n3d_.setInputCloud ( boost::make_shared<const pcl::PointCloud<T1> > (input_cloud) );
  n3d_.compute (output_cloud);

  //* TODO: move the following lines to NormalEstimation class ?
  for ( int i=0; i< (int)input_cloud.points.size (); i++ ){
    output_cloud.points[ i ].x = input_cloud.points[ i ].x;
    output_cloud.points[ i ].y = input_cloud.points[ i ].y;
    output_cloud.points[ i ].z = input_cloud.points[ i ].z;
    output_cloud.points[ i ].rgb = input_cloud.points[ i ].rgb;
  }
}

//-----------
//* voxelize
template <typename T>
void getVoxelGrid( pcl::VoxelGrid<T> &grid, pcl::PointCloud<T> input_cloud, pcl::PointCloud<T>& output_cloud ){
  grid.setLeafSize (downsample_leaf, downsample_leaf, downsample_leaf);
  grid.setInputCloud ( boost::make_shared<const pcl::PointCloud<T> > (input_cloud) );
  grid.setSaveLeafLayout(true);
  grid.filter (output_cloud);
}

//--------------------
//* function for GRSD 
int get_type (float min_radius, float max_radius)
{
  if (min_radius > min_radius_plane_) // 0.066
    return PLANE; // plane
  else if ((min_radius < min_radius_noise_) && (max_radius < max_radius_noise_))
    return NOISE; // noise/corner
  else if (max_radius - min_radius < max_min_radius_diff_) // 0.0075
    return CIRCLE; // circle (corner?)
  else if (min_radius < min_radius_edge_) /// considering small cylinders to be edges
    return EDGE; // edge
  else
    return CYLINDER; // cylinder (rim)
}

//--------------------
//* compute - GRSD -
template <typename T>
void computeGRSD(pcl::VoxelGrid<T> grid, pcl::PointCloud<T> cloud, pcl::PointCloud<T> cloud_downsampled, std::vector<float> &feature ){
  double fixed_radius_search = 0.03;

  // Compute RSD
  pcl::RSDEstimation <T, T, pcl::PrincipalRadiiRSD> rsd;
  rsd.setInputCloud( boost::make_shared<const pcl::PointCloud<T> > (cloud_downsampled) );
  rsd.setSearchSurface( boost::make_shared<const pcl::PointCloud<T> > (cloud) );
  rsd.setInputNormals( boost::make_shared<const pcl::PointCloud<T> > (cloud) );
  ROS_INFO("radius search: %f", std::max(fixed_radius_search, downsample_leaf/2 * sqrt(3)));
  rsd.setRadiusSearch(std::max(fixed_radius_search, downsample_leaf/2 * sqrt(3)));
  ( boost::make_shared<pcl::KdTreeFLANN<T> > () )->setInputCloud ( boost::make_shared<const pcl::PointCloud<T> > (cloud) );
  rsd.setSearchMethod( boost::make_shared<pcl::KdTreeFLANN<T> > () );
  pcl::PointCloud<pcl::PrincipalRadiiRSD> radii;
  rsd.compute(radii);
  
  // Get rmin/rmax for adjacent 27 voxel
  Eigen3::MatrixXi relative_coordinates (3, 13);

  Eigen3::MatrixXi transition_matrix =  Eigen3::MatrixXi::Zero(6, 6);

  int idx = 0;
  
  // 0 - 8
  for( int i=-1; i<2; i++ )
  {
    for( int j=-1; j<2; j++ )
    {
      relative_coordinates( 0, idx ) = i;
      relative_coordinates( 1, idx ) = j;
      relative_coordinates( 2, idx ) = -1;
      idx++;
    }
  }
  // 9 - 11
  for( int i=-1; i<2; i++ )
  {
    relative_coordinates( 0, idx ) = i;
    relative_coordinates( 1, idx ) = -1;
    relative_coordinates( 2, idx ) = 0;
    idx++;
  }
  // 12
  relative_coordinates( 0, idx ) = -1;
  relative_coordinates( 1, idx ) = 0;
  relative_coordinates( 2, idx ) = 0;

  Eigen3::MatrixXi relative_coordinates_all (3, 26);
  relative_coordinates_all.block<3, 13>(0, 0) = relative_coordinates;
  relative_coordinates_all.block<3, 13>(0, 13) = -relative_coordinates;
  
  // Get transition matrix
  std::vector<int> types (radii.points.size());
 
  for (size_t idx = 0; idx < radii.points.size (); ++idx)
    types[idx] = get_type(radii.points[idx].r_min, radii.points[idx].r_max);
  
  for (size_t idx = 0; idx < (boost::make_shared<const pcl::PointCloud<T> > (cloud_downsampled))->points.size (); ++idx)
  {
    int source_type = types[idx];
    std::vector<int> neighbors = grid.getNeighborCentroidIndices ((boost::make_shared<const pcl::PointCloud<T> > (cloud_downsampled))->points[idx], relative_coordinates);
    for (unsigned id_n = 0; id_n < neighbors.size(); id_n++)
    {
      int neighbor_type;
      if (neighbors[id_n] == -1)
        neighbor_type = EMPTY;
      else
        neighbor_type = types[neighbors[id_n]];

      transition_matrix(source_type, neighbor_type)++;
    }
  }

  int nrf = 0;
  pcl::PointCloud<pcl::GRSDSignature21> cloud_grsd;
  cloud_grsd.points.resize(1);
  
  for (int i=0; i<NR_CLASS+1; i++)
  {
    for (int j=i; j<NR_CLASS+1; j++)
    {
      cloud_grsd.points[0].histogram[nrf++] = transition_matrix(i, j); //@TODO: resize point cloud
    }
  }

  feature.resize( 21 );
  for( int i=0; i<21; i++)
    feature[ i ] = cloud_grsd.points[ 0 ].histogram[ i ];
}

//------------------------
//* compute - ColorCHLAC -
template <typename T>
void computeColorCHLAC(pcl::VoxelGrid<T> grid, pcl::PointCloud<T> cloud, std::vector<float> &feature ){
  pcl::PointCloud<ColorCHLACSignature981> colorCHLAC_signature;
  pcl::ColorCHLACEstimation<T> colorCHLAC_;

  colorCHLAC_.setRadiusSearch (1.8);
  colorCHLAC_.setSearchMethod ( boost::make_shared<pcl::KdTreeFLANN<T> > () );
  colorCHLAC_.setColorThreshold( 127, 127, 127 );
  colorCHLAC_.setVoxelFilter (grid);
  colorCHLAC_.setInputCloud ( boost::make_shared<const pcl::PointCloud<T> > (cloud) );
  t1 = my_clock();
  colorCHLAC_.compute( colorCHLAC_signature );
  t2 = my_clock();
  ROS_INFO (" %d colorCHLAC estimated. (%f sec)", (int)colorCHLAC_signature.points.size (), t2-t1);

  feature.resize( DIM_COLOR_1_3_ALL );
  for( int i=0; i<DIM_COLOR_1_3_ALL; i++)
    feature[ i ] = colorCHLAC_signature.points[ 0 ].histogram[ i ];
}

//-------
//* main
int main( int argc, char** argv ){
  if( argc != 2 ){
    ROS_ERROR ("Need one parameter! Syntax is: %s {input_pointcloud_filename.pcd}\n", argv[0]);
    return(-1);
  }
  char filename[ 300 ];
  int length;

  //* read
  pcl::PointCloud<PointXYZRGB> input_cloud;
  readPoints( argv[1], input_cloud );

  //* compute normals
  pcl::PointCloud<PointXYZRGBNormal> cloud;
  computeNormal( input_cloud, cloud );

  //* voxelize
  pcl::VoxelGrid<PointXYZRGBNormal> grid;
  pcl::PointCloud<PointXYZRGBNormal> cloud_downsampled;
  getVoxelGrid( grid, cloud, cloud_downsampled );

  //* compute - GRSD -
  std::vector<float> grsd;
  computeGRSD( grid, cloud, cloud_downsampled, grsd );

  //* compute - ColorCHLAC -
  std::vector<float> colorCHLAC;
  computeColorCHLAC( grid, cloud_downsampled, colorCHLAC );

  //* concatenate GRSD and ColorCHLAC
  std::vector<float> feature;
  feature.resize( 21 + DIM_COLOR_1_3_ALL );
  for( int i=0; i<21; i++)
    feature[ i ] = grsd[ i ];
  for( int i=0; i<DIM_COLOR_1_3_ALL; i++)
    feature[ i + 21 ] = colorCHLAC[ i ];

  //* write
  length = strlen( argv[1] );
  argv[1][ length-4 ] = '\0';
  sprintf(filename,"%s_GRSD_CCHLAC.pcd",argv[1]);
  writeFeature( filename, feature );

  return(0);
}