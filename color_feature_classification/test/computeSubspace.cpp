#include <pcl/features/vfh.h>
#include <vfh_cluster_classifier/common_io.h>
#include <terminal_tools/parse.h>
#include <terminal_tools/print.h>
#include "color_feature_classification/libPCA.hpp"
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree.h>
#include <color_chlac/color_chlac.h>

using namespace pcl;
using namespace terminal_tools;
using namespace std;
using vfh_cluster_classifier::vfh_model;

#define TEST_COLOR_CHLAC
#define SMALL_SAMPLES_FLG false //true

////////////////////////////////////////////////////////////////////////////////
/** \brief Load a set of VFH features that will act as the model (training data)
  * \param argc the number of arguments (pass from main ())
  * \param argv the actual command line arguments (pass from main ())
  * \param extension the file extension containing the VFH features
  * \param models the resultant vector of histogram models
  */
void
  loadFeatureModels (int argc, char **argv, const std::string &extension, 
                     std::vector<vfh_model> &models)
{
  // Suppose the first argument is the actual test model
  for (int i = 1; i < argc; i++)
  {
    string fname = string (argv[i]);
    // Needs to have the right size
    if (fname.size () <= extension.size ())
      continue;
 
    transform (fname.begin (), fname.end (), fname.begin (), (int(*)(int))tolower);
    if (fname.compare (fname.size () - extension.size (), extension.size (), extension) == 0)
    {
      // Load the histogram model and saves it into the global list of models
      vfh_model m;

      if (!vfh_cluster_classifier::loadHist (argv[i], m)){
	m.first = argv[i];
	m.second.resize( DIM_COLOR_BIN_1_3+DIM_COLOR_1_3 );
	// read file normaly
	char line[ 100 ];
	FILE *fp = fopen( argv[i], "r" );
	for( int t=0; t<9; t++ )
	  fgets(line,sizeof(line),fp);
	for(int t=0;t<DIM_COLOR_BIN_1_3+DIM_COLOR_1_3;t++)
	  fscanf(fp,"%f ",&(m.second[ t ]) );
	fclose(fp);
      }
      print_highlight ("Loading %s (%zu models loaded so far).\n", argv[i], models.size ());
      models.push_back (m);

#ifdef TEST_COLOR_CHLAC
      vfh_model m_rotate; m.first = argv[i];
      vfh_model m_rotate_pre = m;
      vfh_model m_rotate_pre2; m_rotate_pre2.first = argv[i];

      for(int t=0;t<3;t++){
	rotateFeature90( m_rotate.second,m_rotate_pre.second,R_MODE_2);
	models.push_back ( m_rotate ); // 1 - 3
	m_rotate_pre = m_rotate;
      }

      rotateFeature90( m_rotate.second,m.second,R_MODE_3);
      models.push_back ( m_rotate ); // 4
      m_rotate_pre  = m_rotate;
      m_rotate_pre2 = m_rotate;

      for(int t=0;t<3;t++){
	rotateFeature90( m_rotate.second,m_rotate_pre.second,R_MODE_2);
	models.push_back ( m_rotate ); // 5 - 7
	m_rotate_pre = m_rotate;
      }

      rotateFeature90( m_rotate.second,m_rotate_pre2.second,R_MODE_3);
      models.push_back ( m_rotate ); // 8
      m_rotate_pre = m_rotate;
      m_rotate_pre2 = m_rotate;

      for(int t=0;t<3;t++){
	rotateFeature90( m_rotate.second,m_rotate_pre.second,R_MODE_2);
	models.push_back ( m_rotate ); // 9 - 11
	m_rotate_pre = m_rotate;
      }

      rotateFeature90( m_rotate.second,m_rotate_pre2.second,R_MODE_3);
      models.push_back ( m_rotate ); // 12
      m_rotate_pre = m_rotate;
      //m_rotate_pre2 = m_rotate;

      for(int t=0;t<3;t++){
	rotateFeature90( m_rotate.second,m_rotate_pre.second,R_MODE_2);
	models.push_back ( m_rotate ); // 13 - 15
	m_rotate_pre = m_rotate;
      }
      
      rotateFeature90( m_rotate.second,m.second,R_MODE_1);
      models.push_back ( m_rotate ); // 16
      m_rotate_pre = m_rotate;

      for(int t=0;t<3;t++){
	rotateFeature90( m_rotate.second,m_rotate_pre.second,R_MODE_2);
	models.push_back ( m_rotate ); // 17 - 19
	m_rotate_pre = m_rotate;
      }

      rotateFeature90( m_rotate.second,m.second,R_MODE_4);
      models.push_back ( m_rotate ); // 20
      m_rotate_pre = m_rotate;

      for(int t=0;t<3;t++){
	rotateFeature90( m_rotate.second,m_rotate_pre.second,R_MODE_2);
	models.push_back ( m_rotate ); // 21 - 23
	m_rotate_pre = m_rotate;
      }
#endif
    }
  }
}

void compressFeature( string filename, std::vector<vfh_model> &models, const int dim, bool ascii ){
  PCA pca;
  pca.read( filename.c_str(), ascii );
  MatrixXf tmpMat = pca.Axis();
  MatrixXf tmpMat2 = tmpMat.block(0,0,tmpMat.rows(),dim);
  const int num = (int)models.size();
  for( int i=0; i<num; i++ ){
    Map<VectorXf> vec( &(models[i].second[0]), models[i].second.size() );
    //vec = tmpMat2.transpose() * vec;
    VectorXf tmpvec = tmpMat2.transpose() * vec;
    models[i].second.resize( dim );
    for( int t=0; t<dim; t++ )
      models[i].second[t] = tmpvec[t];
  }
}

void computeSubspace( std::vector<vfh_model> models, const char* filename, bool ascii ){
  cout << models[0].second.size() << endl;
  PCA pca( false );
  const int num = (int)models.size();
  for( int i=0; i<num; i++ )
    pca.addData( models[ i ].second );
#ifdef TEST_COLOR_CHLAC
  pca.solve();
#else
  pca.solve( SMALL_SAMPLES_FLG, 0.1 );  
#endif
  pca.write( filename, ascii );
}

int main( int argc, char** argv ){
  if( argc < 3 ){
    ROS_ERROR ("Need at least two parameters! Syntax is: %s [model_directory] [options] [output_pca_name]\n", argv[0]);
    ROS_INFO ("    where [options] are:  -dim D = size of compressed feature vectors\n");
    ROS_INFO ("                          -comp filename = name of compress_axis file\n");
    return(-1);
  }
  //argc--; // because argv[ argc-1 ] is not a model file name.

  std::string extension (".pcd");
  transform (extension.begin (), extension.end (), extension.begin (), (int(*)(int))tolower);
  std::vector<vfh_model> models;
  loadFeatureModels (argc, argv, extension, models);

  // Compress the dimension of the vector (if needed)
  int dim;
  if( parse_argument (argc, argv, "-dim", dim) > 0 ){
    if ((dim < 0)||(dim >= (int)models[0].second.size())){
      print_error ("Invalid dimension (%d)!\n", dim);
      return (-1);
    }
    string filename;
    parse_argument (argc, argv, "-comp", filename);
    compressFeature( filename, models, dim, false );
  }

  computeSubspace( models, argv[ argc - 1 ], false );

  return(0);
}
