/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2009, Willow Garage
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

#include <stdlib.h>

#include <vector>
#include <iostream>

#include <point_cloud_mapping/kdtree/kdtree_ann.h>

// include all descriptors for you
#include <ias_descriptors_3d/all_descriptors.h>

//computeCentroid
#include <point_cloud_mapping/geometry/nearest.h>

//ros
#include <sensor_msgs/PointCloud.h>
#include <geometry_msgs/PointStamped.h>
#include "visualization_msgs/Marker.h"
#include "visualization_msgs/MarkerArray.h"
#include <geometry_msgs/Point32.h>
#include <ros/ros.h>

//cloud_algos
#include <cloud_algos/cloud_algos.h>

//bullet
#include <tf/tf.h>

#include <tools/transform.h>
#include <angles/angles.h>
using namespace std;


class BoxFit
{
 public:

  ///////////////////////////////////////////////////////////////////////////////
  /**
   * \brief Contructor
   */
  BoxFit(ros::NodeHandle &n) :
    n_(n)
  {
    cloud_topic_ = "/cloud_out";
    box_topic_ = "/box";
    coeff_.resize(6);
    cloud_pub_ = n_.advertise<sensor_msgs::PointCloud>(cloud_topic_ ,1);
    //box_pub_ = n_.advertise<sensor_msgs::PointCloud>(box_topic_ ,1);
    vis_pub_ = n_.advertise<visualization_msgs::Marker>( "visualization_marker", 0 );
    axis_.x = 1.0;
    axis_.y = 0.0;
    axis_.z = 1.0;
    rotate_inliers_ = -70.0;
    r_ = g_ = 0.0;
    b_ = 1.0;
  }


  ///////////////////////////////////////////////////////////////////////////////
  /**
   * \brief Destructor
   */
  ~BoxFit()
  {
    
  }


 ////////////////////////////////////////////////////////////////////////////////
  /**
   * actual model fitting happens here
   */
  void find_model(const sensor_msgs::PointCloud &cloud, std::vector<double> &coeff)
  {
    // ----------------------------------------------
    // Read point cloud data and create Kd-tree that represents points.
    // We will compute features for all points in the point cloud.
    cloud_kdtree::KdTreeANN data_kdtree(cloud);
    std::vector<const geometry_msgs::Point32*> interest_pts;
    interest_pts.reserve(cloud.points.size());
    //std::vector<const geometry_msgs::Point32*> interest_pts(data.points.size());
    for (size_t i = 0 ; i < cloud.points.size() ; i++)
    {
      interest_pts.push_back(&(cloud.points[i]));
    }
    
    // ----------------------------------------------
    // SpectralAnalysis is not a descriptor, it is a class that holds
    // intermediate data to be used/shared by different descriptors.
    // It performs eigen-analyis of local neighborhoods and extracts
    // eigenvectors and values.  Here, we set it to look at neighborhoods
  // within a radius of 5.0 around each interest point.
    double r = 70.0;
    SpectralAnalysis sa(r);
    BoundingBoxSpectral bbox_spectral(r, sa);
    //BoundingBoxRaw bbox_spectral(70.0);
    
    // ----------------------------------------------
    // Put all descriptors into a vector
    vector<Descriptor3D*> descriptors_3d;
    //   descriptors_3d.push_back(&shape_spectral);
    //   descriptors_3d.push_back(&spin_image1);
    //   descriptors_3d.push_back(&spin_image2);
    //   descriptors_3d.push_back(&o_normal);
    //   descriptors_3d.push_back(&o_tangent);
    //   descriptors_3d.push_back(&position);
    descriptors_3d.push_back(&bbox_spectral);
    
    // ----------------------------------------------
    // Iterate over each descriptor and compute features for each point in the point cloud.
    // The compute() populates a vector of vector of floats, i.e. a feature vector for each
    // interest point.  If the features couldn't be computed successfully for an interest point,
    // its feature vector has size 0
    unsigned int nbr_descriptors = descriptors_3d.size();
    vector<std::vector<std::vector<float> > > all_descriptor_results(nbr_descriptors);
    for (unsigned int i = 0 ; i < nbr_descriptors ; i++)
    {
      descriptors_3d[i]->compute(cloud, data_kdtree, interest_pts, all_descriptor_results[i]);
    }
    
    //   // ----------------------------------------------
    //   // Print out the bounding box dimension features for the first point 0
    std::vector<float>& pt0_bbox_features = all_descriptor_results[0][0];
    cout << "Bounding box features size: " <<  pt0_bbox_features.size() << endl;
    for (size_t i = 0 ; i < pt0_bbox_features.size() ; i++)
    {
      if (i < 12)
      {     
        coeff[i+3] = pt0_bbox_features[i];
      }
      else
        ROS_WARN("Box dimensions bigger than 3 - unusual");
    }
    ROS_INFO("Box dimensions x: %f, y: %f, z: %f ", pt0_bbox_features[0],  pt0_bbox_features[1],  pt0_bbox_features[2]);
    ROS_INFO("Eigen vectors: \n\t%f %f %f \n\t%f %f %f \n\t%f %f %f", pt0_bbox_features[3], pt0_bbox_features[4], 
             pt0_bbox_features[5], pt0_bbox_features[6], pt0_bbox_features[7], pt0_bbox_features[8], 
             pt0_bbox_features[9], pt0_bbox_features[10],pt0_bbox_features[11]);
  }

    
  ////////////////////////////////////////////////////////////////////////////////
  /**
   * \brief publishes model marker (to rviz)
   */  
  void publish_model_rviz (std::vector<double> &coeff)
  {
    btMatrix3x3 box_rot (coeff[6], coeff[7], coeff[8],
                           coeff[9], coeff[10], coeff[11],
                           coeff[12], coeff[13], coeff[14]);
    btMatrix3x3 box_rot_trans (box_rot.transpose());
    btQuaternion qt;
    box_rot_trans.getRotation(qt);

    visualization_msgs::Marker marker;
    marker.header.frame_id = "base_link";
    marker.header.stamp = ros::Time();
    marker.ns = "my_namespace";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::CUBE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = coeff[0];
    marker.pose.position.y = coeff[1];
    marker.pose.position.z = coeff[2];
    marker.pose.orientation.x = qt.x();
    marker.pose.orientation.y = qt.y();
    marker.pose.orientation.z = qt.z();
    marker.pose.orientation.w = qt.w();
    marker.scale.x = coeff[3];
    marker.scale.y = coeff[4];
    marker.scale.z = coeff[5];
    marker.color.a = 1.0;
    marker.color.r = 0.0;
    marker.color.g = 1.0;
    marker.color.b = 0.0;
    ROS_INFO("Publishing on /visualization_marker topic");
    vis_pub_.publish( marker );
  }


  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  /** \brief Create synthetic point cloud
   * \param point cloud
   */
  void create_point_cloud(sensor_msgs::PointCloud& data)
  {
    data.header.frame_id = "/base_link";
    unsigned int nbr_pts = 2000;
    data.points.resize(nbr_pts);
    data.channels.resize(3);
    data.channels[0].name = "r";
    data.channels[1].name = "g";
    data.channels[2].name = "b";
    data.channels[0].values.resize(nbr_pts);
    data.channels[1].values.resize(nbr_pts);
    data.channels[2].values.resize(nbr_pts);

    for (unsigned int i = 0 ; i < nbr_pts ; i++)
    {
      data.points[i].x = rand() % 40;
      data.points[i].y = rand() % 20;
      data.points[i].z = rand() % 50;
      data.channels[0].values[i] = r_;
      data.channels[1].values[i] = g_;
      data.channels[2].values[i] = b_;
    }
    if(rotate_inliers_ != 0.0)
    {
      player_log_actarray::transform::rotatePointCloud(points_, points_rotated_, angles::from_degrees(rotate_inliers_), axis_);
      data = points_rotated_;
    }
  }


  ////////////////////////////////////////////////////////////////////////////////
  /**
   * \brief spin
   */
  bool spin ()
  {
    ros::Duration delay(5.0);
    while (n_.ok())
    { 
      //process part
      create_point_cloud(points_);
      cloud_geometry::nearest::computeCentroid (points_, box_centroid_);
      coeff_[0] = box_centroid_.x;
      coeff_[1] = box_centroid_.y;
      coeff_[2] = box_centroid_.z;
      find_model(points_, coeff_);
      ROS_INFO ("Publishing data on topic %s with nr of points %ld", n_.resolveName (cloud_topic_).c_str (),
                points_.points.size()); 
      cloud_pub_.publish(points_);
      publish_model_rviz(coeff_);
      
      //sleeping part
      ros::spinOnce();
      delay.sleep();
    }
    return true;
  }
  
protected:
  sensor_msgs::PointCloud points_;
  sensor_msgs::PointCloud box_points_;
  sensor_msgs::PointCloud points_rotated_;
  ros::NodeHandle n_;
  //publishes original, input cloud
  ros::Publisher cloud_pub_;
  //publishes points inside the box
  ros::Publisher box_pub_;
  //subscribes to input cloud (cluster)
  ros::Subscriber clusters_sub_;
  //model rviz publisher
  ros::Publisher vis_pub_;
  //box coefficients: cx, cy, cz, dx, dy, dz
  std::vector<double> coeff_;
  geometry_msgs::Point32 box_centroid_;
  std::string cloud_topic_, box_topic_;
  geometry_msgs::Point32 axis_;
  float rotate_inliers_;

  //point color
  float r_, g_, b_;
};


int
main (int argc, char** argv)
{
  ros::init(argc, argv, "box_fit");
  ros::NodeHandle n;
  BoxFit box(n);
  box.spin();
  return 0;
}



// void createPointCloud(sensor_msgs::PointCloud& data);

// // --------------------------------------------------------------
// /*!
//  * \brief Example program demonstrating how to use the desciptors_3d package.
//  */
// // --------------------------------------------------------------
// int main()
// {
//   // ----------------------------------------------
//   // Read point cloud data and create Kd-tree that represents points.
//   // We will compute features for all points in the point cloud.
//   sensor_msgs::PointCloud data;
//   createPointCloud(data);
//   cloud_kdtree::KdTreeANN data_kdtree(data);
//   std::vector<const geometry_msgs::Point32*> interest_pts;
//   interest_pts.reserve(data.points.size());
//   //std::vector<const geometry_msgs::Point32*> interest_pts(data.points.size());
//   for (size_t i = 0 ; i < data.points.size() ; i++)
//   {
//     interest_pts.push_back(&(data.points[i]));
//   }
  
//   // ----------------------------------------------
//   // SpectralAnalysis is not a descriptor, it is a class that holds
//   // intermediate data to be used/shared by different descriptors.
//   // It performs eigen-analyis of local neighborhoods and extracts
//   // eigenvectors and values.  Here, we set it to look at neighborhoods
//   // within a radius of 5.0 around each interest point.
//     SpectralAnalysis sa(5.0);

//   // ----------------------------------------------
//   // Examines the eigenvalues for a local neighborhood scatter matrix
//   // to give features that represent the local shape (how flat, how linear,
//   // how scattered) of the neighborhood.
//   // This descriptor uses information from spectral analysis.
//   //  ShapeSpectral shape_spectral(sa);

//   // ----------------------------------------------
//   // Compute a spin image centered at each interest point.
//   // The spin axis here is the +z direction (0,0,1).
//   // The spin image resolution has 1.0 square cells.
//   // Note that the number of rows in the spin image MUST be odd
//   // (see documentation).
//   // The last argument is used when computing spin image for
//   // regions of points, since we are computing a descriptor
//   // for single points it should be false.
//   //  SpinImageCustom spin_image1(0, 0, 1, 1.0, 1.0, 5, 4, false);

//   // ----------------------------------------------
//   // Compute another spin image centered at each interest point.
//   // The spin axis here is about each interest points' estimated normal.
//   // We can either recompute the normals using a different radius than
//   // used with spectral_shape (by instantiating another SpectralAnalysis),
//   // or we can use the same normals computed from shape_spectral.
//   //  SpinImageNormal spin_image2(1.0, 1.0, 5, 4, false, sa);

//   // ----------------------------------------------
//   // Compares the locally estimated normal and tangent vectors around
//   // each interest point against the specified reference direction (+z).
//   // The feature value is cos(theta), where theta is the angle between
//   // the normal/tangent and the reference direction
//   //  OrientationNormal o_normal(0, 0, 1, sa);
//   //OrientationTangent o_tangent(0, 0, 1, sa);

//   // ----------------------------------------------
//   // The feature is simply the z coordinate for each interest point
//   //  Position position;

//   // ----------------------------------------------
//   // Computes the bounding box in the principle component space
//   // of all points within radius 6.0.  That is, feature values
//   // are the lengths of the box along the 3 component directions.
//   BoundingBoxSpectral bbox_spectral(6.0, sa);

//   // ----------------------------------------------
//   // Put all descriptors into a vector
//   vector<Descriptor3D*> descriptors_3d;
// //   descriptors_3d.push_back(&shape_spectral);
// //   descriptors_3d.push_back(&spin_image1);
// //   descriptors_3d.push_back(&spin_image2);
// //   descriptors_3d.push_back(&o_normal);
// //   descriptors_3d.push_back(&o_tangent);
// //   descriptors_3d.push_back(&position);
//    descriptors_3d.push_back(&bbox_spectral);

//   // ----------------------------------------------
//   // Iterate over each descriptor and compute features for each point in the point cloud.
//   // The compute() populates a vector of vector of floats, i.e. a feature vector for each
//   // interest point.  If the features couldn't be computed successfully for an interest point,
//   // its feature vector has size 0
//    unsigned int nbr_descriptors = descriptors_3d.size();
//    vector<std::vector<std::vector<float> > > all_descriptor_results(nbr_descriptors);
//    for (unsigned int i = 0 ; i < nbr_descriptors ; i++)
//    {
//      descriptors_3d[i]->compute(data, data_kdtree, interest_pts, all_descriptor_results[i]);
//    }
     
// //   // ----------------------------------------------
// //   // Print out the bounding box dimension features for the first point 0
//    std::vector<float>& pt0_bbox_features = all_descriptor_results[0][0];
//    cout << "Bounding box features size: " <<  pt0_bbox_features.size() << endl;
//    for (size_t i = 0 ; i < pt0_bbox_features.size() ; i++)
//    {
//      cout << " " << pt0_bbox_features[i];
//    }
//    cout << endl;
//   return 0;
// }

// // Generates random point cloud
// void createPointCloud(sensor_msgs::PointCloud& data)
// {
//   unsigned int nbr_pts = 5000;
//   data.points.resize(nbr_pts);

//   for (unsigned int i = 0 ; i < nbr_pts ; i++)
//   {
//     data.points[i].x = rand() % 50;
//     data.points[i].y = rand() % 50;
//     data.points[i].z = rand() % 50;
//   }
// }
