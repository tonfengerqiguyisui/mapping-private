#define QUIET

#include <color_feature_classification/points_tools.hpp>
#include <pcl/features/vfh.h>
#include <pcl/features/normal_3d.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree.h>
#include <pcl/io/pcd_io.h>
#include <terminal_tools/parse.h>
#include <terminal_tools/print.h>
#include <color_chlac/grsd_colorCHLAC_tools.h>
#include "color_feature_classification/libPCA.hpp"
#include "FILE_MODE"

using namespace pcl;
using namespace std;
using namespace terminal_tools;

// color threshold
int thR, thG, thB;
float voxel_size;
int offset_step = 1; // the step size of offset voxel number for subdivisions

void
computeFeatureModels ( const char feature_type, const int rotate_step_num, int argc, char **argv, const std::string &extension, 
		       std::vector< std::vector<float> > &models, const int subdivision_size)
{  
  int repeat_num_offset = ceil(subdivision_size / offset_step);
  if( subdivision_size == 0 ) repeat_num_offset = 1;

  pcl::PointCloud<PointXYZRGB> input_cloud;

  if( feature_type == 'd' ){
    pcl::PointCloud<PointXYZRGBNormal> cloud_normal;
    pcl::PointCloud<PointXYZRGBNormal> cloud_normal_r;
    pcl::VoxelGrid<PointXYZRGBNormal> grid_normal;
    pcl::PointCloud<PointXYZRGBNormal> cloud_downsampled_normal;
    
    for (int i = 1; i < argc; i++){
      string fname = string (argv[i]);
      // Needs to have the right size
      if (fname.size () <= extension.size ())
	continue;
      transform (fname.begin (), fname.end (), fname.begin (), (int(*)(int))tolower);
      if (fname.compare (fname.size () - extension.size (), extension.size (), extension) == 0){
	
	readPoints( argv[i], input_cloud );
	
	std::vector< std::vector<float> > grsd;
	std::vector< std::vector<float> > colorCHLAC;
	
	//* compute normals
	computeNormal( input_cloud, cloud_normal );
	
	for(int r3=0; r3 < rotate_step_num; r3++){
	  for(int r2=0; r2 < rotate_step_num; r2++){
	    for(int r1=0; r1 < rotate_step_num; r1++){
	      const double roll  = r3 * M_PI / (2*rotate_step_num);
	      const double pan   = r2 * M_PI / (2*rotate_step_num);
	      const double roll2 = r1 * M_PI / (2*rotate_step_num);
	      
	      //* voxelize
	      rotatePoints( cloud_normal, cloud_normal_r, roll, pan, roll2 );
	      getVoxelGrid( grid_normal, cloud_normal_r, cloud_downsampled_normal, voxel_size );	      

	      // repeat with changing offset values for subdivisions
	      for( int ox = 0; ox < repeat_num_offset; ox++ ){
		for( int oy = 0; oy < repeat_num_offset; oy++ ){
		  for( int oz = 0; oz < repeat_num_offset; oz++ ){
		    //* compute features
		    computeGRSD( grid_normal, cloud_normal_r, cloud_downsampled_normal, grsd, voxel_size, subdivision_size, ox*offset_step, oy*offset_step, oz*offset_step );
		    computeColorCHLAC( grid_normal, cloud_downsampled_normal, colorCHLAC, thR, thG, thB, subdivision_size, ox*offset_step, oy*offset_step, oz*offset_step );		  
		    const int hist_num = colorCHLAC.size();
		  
		    for( int h=0; h<hist_num; h++ )
		      models.push_back ( conc_vector( grsd[ h ], colorCHLAC[ h ] ) );
		  }
		}
	      }

	      if(r2==0) break;
	    }
	  }
	}
      }
    }
  }
  else{ // == 'c'    
    pcl::PointCloud<PointXYZRGB> input_cloud_r; // rotate
    pcl::VoxelGrid<PointXYZRGB> grid;
    pcl::PointCloud<PointXYZRGB> cloud_downsampled;
    
    for (int i = 1; i < argc; i++){
      string fname = string (argv[i]);
      // Needs to have the right size
      if (fname.size () <= extension.size ())
	continue;
      transform (fname.begin (), fname.end (), fname.begin (), (int(*)(int))tolower);
      if (fname.compare (fname.size () - extension.size (), extension.size (), extension) == 0){
	
	readPoints( argv[i], input_cloud );
	
	std::vector< std::vector<float> > grsd;
	std::vector< std::vector<float> > colorCHLAC;
      
	for(int r3=0; r3 < rotate_step_num; r3++){
	  for(int r2=0; r2 < rotate_step_num; r2++){
	    for(int r1=0; r1 < rotate_step_num; r1++){
	      const double roll  = r3 * M_PI / (2*rotate_step_num);
	      const double pan   = r2 * M_PI / (2*rotate_step_num);
	      const double roll2 = r1 * M_PI / (2*rotate_step_num);
	    
	      //* voxelize
	      rotatePoints( input_cloud, input_cloud_r, roll, pan, roll2 );
	      //* voxelize
	      getVoxelGrid( grid, input_cloud_r, cloud_downsampled, voxel_size );

	      // repeat with changing offset values for subdivisions
	      for( int ox = 0; ox < repeat_num_offset; ox++ ){
		for( int oy = 0; oy < repeat_num_offset; oy++ ){
		  for( int oz = 0; oz < repeat_num_offset; oz++ ){
		    //* compute features
		    computeColorCHLAC( grid, cloud_downsampled, colorCHLAC, thR, thG, thB, subdivision_size, ox*offset_step, oy*offset_step, oz*offset_step );		  
		    const int hist_num = colorCHLAC.size();
		  
		    for( int h=0; h<hist_num; h++ ){
		      models.push_back (colorCHLAC[ h ]);
	      
		      std::vector<float> colorCHLAC_rotate;
		      std::vector<float> colorCHLAC_rotate_pre = colorCHLAC[ h ];
		      std::vector<float> colorCHLAC_rotate_pre2;
	      
		      for(int t=0;t<3;t++){
			rotateFeature90( colorCHLAC_rotate,colorCHLAC_rotate_pre,R_MODE_2);
			models.push_back (colorCHLAC_rotate);
			colorCHLAC_rotate_pre = colorCHLAC_rotate;
		      }
	      
		      rotateFeature90( colorCHLAC_rotate,colorCHLAC[ h ],R_MODE_3);
		      models.push_back (colorCHLAC_rotate);
		      colorCHLAC_rotate_pre  = colorCHLAC_rotate;
		      colorCHLAC_rotate_pre2 = colorCHLAC_rotate;
	      
		      for(int t=0;t<3;t++){
			rotateFeature90( colorCHLAC_rotate,colorCHLAC_rotate_pre,R_MODE_2);
			models.push_back (colorCHLAC_rotate);
			colorCHLAC_rotate_pre = colorCHLAC_rotate;
		      }
	      
		      rotateFeature90( colorCHLAC_rotate,colorCHLAC_rotate_pre2,R_MODE_3);
		      models.push_back (colorCHLAC_rotate);
		      colorCHLAC_rotate_pre = colorCHLAC_rotate;
		      colorCHLAC_rotate_pre2 = colorCHLAC_rotate;
	      
		      for(int t=0;t<3;t++){
			rotateFeature90( colorCHLAC_rotate,colorCHLAC_rotate_pre,R_MODE_2);
			models.push_back (colorCHLAC_rotate);
			colorCHLAC_rotate_pre = colorCHLAC_rotate;
		      }
	      
		      rotateFeature90( colorCHLAC_rotate,colorCHLAC_rotate_pre2,R_MODE_3);
		      models.push_back (colorCHLAC_rotate);
		      colorCHLAC_rotate_pre = colorCHLAC_rotate;
		      //colorCHLAC_rotate_pre2 = colorCHLAC_rotate;
	      
		      for(int t=0;t<3;t++){
			rotateFeature90( colorCHLAC_rotate,colorCHLAC_rotate_pre,R_MODE_2);
			models.push_back (colorCHLAC_rotate);
			colorCHLAC_rotate_pre = colorCHLAC_rotate;
		      }
	      
		      rotateFeature90( colorCHLAC_rotate,colorCHLAC[ h ],R_MODE_1);
		      models.push_back (colorCHLAC_rotate);
		      colorCHLAC_rotate_pre = colorCHLAC_rotate;
	      
		      for(int t=0;t<3;t++){
			rotateFeature90( colorCHLAC_rotate,colorCHLAC_rotate_pre,R_MODE_2);
			models.push_back (colorCHLAC_rotate);
			colorCHLAC_rotate_pre = colorCHLAC_rotate;
		      }
	      
		      rotateFeature90( colorCHLAC_rotate,colorCHLAC[ h ],R_MODE_4);
		      models.push_back (colorCHLAC_rotate);
		      colorCHLAC_rotate_pre = colorCHLAC_rotate;
	      
		      for(int t=0;t<3;t++){
			rotateFeature90( colorCHLAC_rotate,colorCHLAC_rotate_pre,R_MODE_2);
			models.push_back (colorCHLAC_rotate);
			colorCHLAC_rotate_pre = colorCHLAC_rotate;
		      }
		    }
		  }
		}
	      }

	      if(r2==0) break;
	    }
	  }
	}
      }
    }
  }
}

void compressFeature( string filename, std::vector< std::vector<float> > &models, const int dim, bool ascii ){
  PCA pca;
  pca.read( filename.c_str(), ascii );
  VectorXf variance = pca.Variance();
  MatrixXf tmpMat = pca.Axis();
  MatrixXf tmpMat2 = tmpMat.block(0,0,tmpMat.rows(),dim);
  const int num = (int)models.size();
  for( int i=0; i<num; i++ ){
    Map<VectorXf> vec( &(models[i][0]), models[i].size() );
    //vec = tmpMat2.transpose() * vec;
    VectorXf tmpvec = tmpMat2.transpose() * vec;
    models[i].resize( dim );
    if( WHITENING ){
      for( int t=0; t<dim; t++ )
	models[i][t] = tmpvec[t] / sqrt( variance( t ) );
    }
    else{
      for( int t=0; t<dim; t++ )
	models[i][t] = tmpvec[t];
    }
  }
}

// bool if_zero_vec( const std::vector<float> vec ){
//   const int vec_size = vec.size();
//   for( int i=0; i<vec_size; i++ )
//     if( vec[ i ] != 0 ) return false;
//   return true;
// }

void computeSubspace( std::vector< std::vector<float> > models, const char* filename, bool ascii ){
  cout << models[0].size() << endl;
  PCA pca( false );
  const int num = (int)models.size();
  for( int i=0; i<num; i++ )
    if( !if_zero_vec( models[ i ] ) )
      pca.addData( models[ i ] );
  pca.solve();
  pca.write( filename, ascii );
}

int main( int argc, char** argv ){
  if( argc < 4 ){
    ROS_ERROR ("Need at least three parameters! Syntax is: %s {feature_initial(c or d)} [model_directory] [options] [output_pca_name]\n", argv[0]);
    ROS_INFO ("    where [options] are:  -dim D = size of compressed feature vectors\n");
    ROS_INFO ("                          -comp filename = name of compress_axis file\n");
    ROS_INFO ("                          -rotate rotate_step_num = e.g. 3 for 30 degrees rotation\n");
    ROS_INFO ("                          -subdiv N = subdivision size (e.g. 10 voxels)\n");
    ROS_INFO ("                          -offset n = offset step for subdivisions (e.g. 5 voxels)\n");
    return(-1);
  }

  // check feature_type
  const char feature_type = argv[1][0];
  if( (feature_type != 'c') && (feature_type != 'd') ){
    ROS_ERROR ("Unknown feature type.\n");
    return(-1);
  }

  // rotate step num
  int rotate_step_num = 1;
  if( parse_argument (argc, argv, "-rotate", rotate_step_num) > 0 ){
    if ( rotate_step_num < 1 ){
      print_error ("Invalid rotate_step_num (%d)!\n", rotate_step_num);
      return (-1);
    }
  }

  // color threshold
  FILE *fp = fopen( "color_threshold.txt", "r" );
  fscanf( fp, "%d %d %d\n", &thR, &thG, &thB );
  fclose(fp);

  // voxel size
  fp = fopen( "voxel_size.txt", "r" );
  fscanf( fp, "%f\n", &voxel_size );
  fclose(fp);

  // subdivision size
  int subdivision_size = 0;
  if( parse_argument (argc, argv, "-subdiv", subdivision_size) > 0 ){
    if ( subdivision_size < 0 ){
      print_error ("Invalid subdivision size (%d)! \n", subdivision_size);
      return (-1);
    }
  }

  // offset step
  if( parse_argument (argc, argv, "-offset", offset_step) > 0 ){
    if ( ( offset_step < 1 ) || ( offset_step >= subdivision_size ) ){
      print_error ("Invalid offset step (%d)! (while subdivision size is %d.)\n", offset_step, subdivision_size);
      return (-1);
    }
  }

  // compute features
  std::string extension (".pcd");
  transform (extension.begin (), extension.end (), extension.begin (), (int(*)(int))tolower);
  std::vector< std::vector<float> > models;
  computeFeatureModels (feature_type, rotate_step_num, argc, argv, extension, models, subdivision_size);

  // compress the dimension of the vector (if needed)
  int dim;
  if( parse_argument (argc, argv, "-dim", dim) > 0 ){
    if ((dim < 0)||(dim >= (int)models[0].size())){
      print_error ("Invalid dimension (%d)!\n", dim);
      return (-1);
    }
    string filename;
    parse_argument (argc, argv, "-comp", filename);
    compressFeature( filename, models, dim, false );
  }

  // compute subspace
  computeSubspace( models, argv[ argc - 1 ], false );

  return(0);
}
