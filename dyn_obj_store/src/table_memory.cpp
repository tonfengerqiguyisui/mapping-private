// #include <unistd.h>

#include <map>
#include <ctime>
#include <ros/node_handle.h>
#include <ias_table_msgs/TableWithObjects.h>
#include <ias_table_msgs/PrologReturn.h>
#include <ias_table_srvs/ias_table_clusters_service.h>
#include <ias_table_srvs/ias_reconstruct_object.h>
#include <point_cloud_mapping/cloud_io.h>
#include <point_cloud_mapping/geometry/areas.h>
#include <point_cloud_mapping/geometry/statistics.h>
#include <mapping_msgs/PolygonalMap.h>
#include <geometry_msgs/Polygon.h>

// cloud_algos plugin stuff
#include <cloud_algos/rotational_estimation.h>
#include <pluginlib/class_loader.h>

// COP/JLO stuff
#include <vision_srvs/srvjlo.h>
#include <vision_srvs/cop_call.h>
#include <vision_msgs/partial_lo.h>
#include <vision_msgs/cop_answer.h>
#include <vision_srvs/clip_polygon.h>

using namespace cloud_algos;

struct dummy_deleter
{
  void operator()(void const *) const
  {
  }
};


/// holds a single object on a table, with timestamp, orig. pcd data and reconstructed representations
struct TableObject
{
  TableObject (): type (-1), lo_id (0) { }
  geometry_msgs::Point32 center;
  sensor_msgs::PointCloud point_cluster;
  geometry_msgs::Point32 minP;
  geometry_msgs::Point32 maxP;
  int type;
  unsigned long long lo_id;
  std::vector <double> coeffs;
  double score;
  std::vector<int> triangles;
  std::string semantic_type;
  std::vector<std::string> color;
};

/// holds a single snapshot of a table
struct TableStateInstance
{
  ros::Time time_instance;
  std::vector<TableObject*> objects;
};

/// this contains a "timeline" of table snapshots for one table
struct Table
{
  unsigned new_flag;
  geometry_msgs::Point32 center;
  geometry_msgs::Polygon polygon;
  int color;

  std::vector<TableStateInstance*> inst;
  
  TableStateInstance *getCurrentInstance ()
  {

    if (inst.size() == 0)
    {
      ROS_ERROR ("TableStateInstance requested in empty list.");
      return new TableStateInstance ();
    }

    return inst.back ();
  }

  std::vector<TableStateInstance*> getLastInstances (unsigned int n)
  {
    std::vector<TableStateInstance*> ret;
    for (std::vector<TableStateInstance*>::reverse_iterator it = inst.rbegin (); it != inst.rend (); it++)
      {
        if (n != 0)
          {
            ret.push_back(*it);
            n--;
          }
        else
          break;
      }
    return ret;
  }

  TableStateInstance *getInstanceAtTime (ros::Time t)
  {
    TableStateInstance* ret = inst.back ();
    for (std::vector<TableStateInstance*>::reverse_iterator it = inst.rbegin (); it != inst.rend (); it++)
      if ((*it)->time_instance <= t)
        ret = *it;
      else
        break;
    return ret;
  }

};

/// this should be called dyn_obj_store
class TableMemory
{
  protected:
    ros::NodeHandle &nh_;

    // topic names
    std::string input_table_topic_;
    std::string input_cop_topic_;
    std::string output_cloud_topic_;
    std::string output_table_state_topic_;

    // publishers and subscribers
    ros::Publisher mem_state_pub_;
    ros::Publisher cloud_pub_;
    ros::Subscriber table_sub_;
    ros::Subscriber cop_sub_;

    // service call related stuff
    ros::ServiceServer table_memory_clusters_service_;
    ros::ServiceClient table_reconstruct_clusters_client_;

    int counter_;
    float color_probability_;
    std::string global_frame_;

    //insert lo_ids waiting for prolog update
    std::vector<unsigned long long> update_prolog_;
    unsigned long long lo_id_tmp_;

    // THE structure... :D 
    std::vector<Table> tables;
    /** @todo have a cupboards structure as well */
    
    // plugin loader
    pluginlib::ClassLoader<CloudAlgo> *cl;
    CloudAlgo * alg_rot_est;
 
    // this is a map<LO Id, vector[tableId, instId, objectId]> that maps a LO id to indices into our tables struct
    std::map <unsigned long long, std::vector<long> > lo_ids;
    TableObject *getObjectFromLOId (unsigned int id)
    {
      std::vector<long> idxs = lo_ids[id];
      return tables[idxs[0]].inst[idxs[1]]->objects[idxs[2]];
    }

    ias_table_msgs::PrologReturn getPrologReturn(unsigned long long id)
    {
      ias_table_msgs::PrologReturn ret;
      std::map <unsigned long long, std::vector<long> >::iterator lo_ids_it;
      lo_ids_it = lo_ids.find(id);
      if (lo_ids_it != lo_ids.end())
      {
        std::vector<long>idxs=lo_ids_it->second;
        ret.table_id = idxs[0];
        ret.table_center =  tables[idxs[0]].center;
        ret.stamp =  tables[idxs[0]].inst[idxs[1]]->time_instance;
        ret.cluster_center =  tables[idxs[0]].inst[idxs[1]]->objects[idxs[2]]->center;
      }
      else
      {
        ROS_ERROR("Id %lld not found!!!", id);
      }
      return ret;
     }

  public:
    TableMemory (ros::NodeHandle &anode) : nh_(anode), counter_(0), color_probability_(0.2), lo_id_tmp_(0)
    {
      nh_.param ("input_table_topic", input_table_topic_, std::string("table_with_objects"));       // 15 degrees
      nh_.param ("input_cop_topic", input_cop_topic_, std::string("/tracking/out"));       // 15 degrees
      nh_.param ("output_cloud_topic", output_cloud_topic_, std::string("table_mem_state_point_clusters"));       // 15 degrees
      nh_.param ("output_table_state_topic", output_table_state_topic_, std::string("table_mem_state"));       // 15 degrees
      nh_.param ("/global_frame_id", global_frame_, std::string("/map"));

      table_sub_ = nh_.subscribe (input_table_topic_, 1, &TableMemory::table_cb, this);
//       cop_sub_ = nh_.subscribe (input_cop_topic_, 1, &TableMemory::cop_cb, this);
      mem_state_pub_ = nh_.advertise<mapping_msgs::PolygonalMap> (output_table_state_topic_, 1);
      cloud_pub_ = nh_.advertise<sensor_msgs::PointCloud> (output_cloud_topic_, 1);
      table_memory_clusters_service_ = nh_.advertiseService ("table_memory_clusters_service", &TableMemory::clusters_service, this);
      
      load_plugins ();
    }
    
    bool compare_table (Table& old_table, const ias_table_msgs::TableWithObjects::ConstPtr& new_table)
    {
      ros::ServiceClient polygon_clipper = nh_.serviceClient<vision_srvs::clip_polygon> ("/intersect_poly", true);
      if (polygon_clipper.exists() && old_table.polygon.points.size() > 2 &&new_table->table.points.size() > 2)
      {
        ROS_WARN ("blablabla");
        vision_srvs::clip_polygon::Request req;
        req.operation = req.INTERSECTION;
        req.poly1 = old_table.polygon;
        req.poly2 = new_table->table; 
        vision_srvs::clip_polygon::Response resp;
        ROS_WARN ("blablabla");
        
        polygon_clipper.call (req, resp);
        ROS_WARN ("blablabla");
        std::vector<double> normal_z(3);
        normal_z[0] = 0.0;
        normal_z[1] = 0.0;
        normal_z[2] = 1.0;
        double area_old = cloud_geometry::areas::compute2DPolygonalArea (old_table.polygon, normal_z);
        double area_new = cloud_geometry::areas::compute2DPolygonalArea (new_table->table, normal_z);
        double area_clip = cloud_geometry::areas::compute2DPolygonalArea (resp.poly_clip, normal_z);
        ROS_WARN ("The 3 areas are: %f, %f, %f: [%f percent, %f percent]", 
                  area_old, area_new, area_clip, area_clip/area_old, area_clip/area_new);
        if (area_clip/area_old > 0.5 || area_clip/area_new > 0.5)
          return true;
      }

      // if center of new (invcomplete) table is within old 
      // table bounds, it's the same
      geometry_msgs::Point32 center;
      center.x = new_table->table_min.x + (new_table->table_max.x - new_table->table_min.x) / 2.0;
      center.y = new_table->table_min.y + (new_table->table_max.y - new_table->table_min.y) / 2.0;
      center.z = new_table->table_min.z + (new_table->table_max.z - new_table->table_min.z) / 2.0;
      if (cloud_geometry::areas::isPointIn2DPolygon (center, old_table.polygon))
        return true;
        
      double x = center.x;
      double y = center.y;
      double z = center.z;

      std::cerr << "Table compare returns false. Center = <" << x << "," << y << "," << z << ">." << std::endl; 
      for (unsigned int i = 0; i < old_table.polygon.points.size() ; i ++)
        std::cerr << "\t Polygon points " << i << " = <" << old_table.polygon.points[i].x
                  << "," << old_table.polygon.points[i].y
                  << "," << old_table.polygon.points[i].z
                  << ">." << std::endl; 
      return false;
    }

    void
      update_table (int table_num, const ias_table_msgs::TableWithObjects::ConstPtr& new_table)
    {
      Table &old_table = tables[table_num];
      ROS_INFO ("Updating table with new TableInstance.");
      TableStateInstance *inst = new TableStateInstance ();
      for (unsigned int i = 0; i < new_table->objects.size(); i++)
      {
        TableObject *to = new TableObject ();

        to->point_cluster = new_table->objects[i].points;
        cloud_geometry::statistics::getMinMax (to->point_cluster, to->minP, to->maxP);

        to->center.x = to->minP.x + (to->maxP.x - to->minP.x) * 0.5;
        to->center.y = to->minP.y + (to->maxP.y - to->minP.y) * 0.5;
        to->center.z = to->minP.z + (to->maxP.z - to->minP.z) * 0.5;

        inst->objects.push_back (to);
      }
      inst->time_instance = new_table->header.stamp;
      old_table.inst.push_back (inst);
      
      ros::ServiceClient polygon_clipper = nh_.serviceClient<vision_srvs::clip_polygon> ("/intersect_poly", true);
      if (polygon_clipper.exists())
      {
        ROS_WARN ("blablabla");
        vision_srvs::clip_polygon::Request req;
        req.operation = req.UNION;
        req.poly1 = old_table.polygon;
        req.poly2 = new_table->table; 
        double z_mean = 0.0;
        for (unsigned int poly_i = 0; poly_i < req.poly1.points.size(); poly_i++)
          z_mean += req.poly1.points.at(poly_i).z;
        for (unsigned int poly_i = 0; poly_i < req.poly2.points.size(); poly_i++)
          z_mean += req.poly2.points.at(poly_i).z;
        z_mean /= (req.poly2.points.size () + req.poly1.points.size());

        vision_srvs::clip_polygon::Response resp;
        ROS_WARN ("blablabla");
        
        polygon_clipper.call (req, resp);
        for (unsigned int poly_i = 0; poly_i < resp.poly_clip.points.size(); poly_i++)
          resp.poly_clip.points.at(poly_i).z = z_mean;
        ROS_WARN ("blablabla");
        std::vector<double> normal_z(3);
        normal_z[0] = 0.0;
        normal_z[1] = 0.0;
        normal_z[2] = 1.0;
        double area_old = cloud_geometry::areas::compute2DPolygonalArea (old_table.polygon, normal_z);
        double area_new = cloud_geometry::areas::compute2DPolygonalArea (new_table->table, normal_z);
        double area_clip = cloud_geometry::areas::compute2DPolygonalArea (resp.poly_clip, normal_z);
        ROS_WARN ("The 3 areas are: %f, %f, %f: [%f percent, %f percent]", 
                  area_old, area_new, area_clip, area_clip/area_old, area_clip/area_new);
        old_table.polygon = resp.poly_clip;
      }
    }
   
    // service call from PROLOG 
    bool
      clusters_service (ias_table_srvs::ias_table_clusters_service::Request &req, 
                          ias_table_srvs::ias_table_clusters_service::Response &resp)
    {
      ROS_INFO("Tables to update: %ld", update_prolog_.size());
      for (unsigned int up = 0; up < update_prolog_.size(); up++)
        {
	  ias_table_msgs::PrologReturn pr =  getPrologReturn (update_prolog_[up]);
          resp.prolog_return.push_back(pr);
        }
      //TODO lock
      update_prolog_.clear();
      return true;
    }

    void cop_cb (const boost::shared_ptr<const vision_msgs::cop_answer> &msg)
    {
      ROS_INFO ("got answer from cop! (Errors: %s)\n", msg->error.c_str());
      for(unsigned int i = 0; i < msg->found_poses.size(); i++)
      {
        const vision_msgs::aposteriori_position &pos =  msg->found_poses [i];
        ROS_INFO ("Found Obj nr %d with prob %f at pos %d\n", (int)pos.objectId, pos.probability, (int)pos.position);
        //this here asumes that color classes are returned in FIFO fashion wrt to cop query (see cop_call function)!!!
        if(pos.probability >= color_probability_)
        {
          TableObject * to = getObjectFromLOId (pos.position);
          for (unsigned int cls = 0; cls < pos.classes.size (); cls++)
          {
            ROS_INFO("Object color is %s", pos.classes[cls].c_str());
            //possible returns: [red (or any other color), object type (Jug)]
            //color is a vector of strings in case object contains multiple color hypotheses
            to->color.push_back(pos.classes[cls]);
          }
        }
      }
      ROS_INFO ("End!\n");
    }
  

    bool update_jlo (int table_num)
    {
      ros::ServiceClient jlo_client_ = nh_.serviceClient<vision_srvs::srvjlo> ("/located_object", true);

      // TODO: 
      //if (!jlo_client_.exists ()) return false;

      for (unsigned int o_idx = 0; o_idx < tables[table_num].getCurrentInstance ()->objects.size (); o_idx++)
      {
        TableObject *o = tables[table_num].getCurrentInstance ()->objects [o_idx];
        geometry_msgs::Point32 extents;
        extents.x = o->center.x - o->minP.x;
        extents.y = o->center.y - o->minP.y;
        extents.z = o->center.z - o->minP.z;
        
        // create service client call to jlo
        vision_srvs::srvjlo call;
        call.request.command = "update";
        //world frame
        call.request.query.parent_id = 1;
        call.request.query.id = 0;

        //fill in pose
        int width = 4;
        for(int r = 0; r < width; r++)
        {
          for(int c = 0; c < width; c++)
          {
            if(r == c)
              call.request.query.pose[r * width + c] = 1;
            else
              call.request.query.pose[r * width + c] = 0;
          }
        }
        
        call.request.query.pose[3]  = o->center.x;
        call.request.query.pose[7]  = o->center.y;
        call.request.query.pose[11] = o->center.z;

        //fill in covariance matrix: 0.9 * 1/2*max(cluster)-min(cluster) [m]
        width = 6;
        for(int r = 0; r < width; r++)
        {
          for(int c = 0; c < width; c++)
          {
            if(r == c)
              call.request.query.cov [r * width + c] = 0.0;
            else
              call.request.query.cov [r * width + c] = 0;
          }
        }

        call.request.query.cov [0]  = extents.x * 0.9;
        call.request.query.cov [7]  = extents.y * 0.9;
        call.request.query.cov [13] = extents.z * 0.9;

        if (!jlo_client_.call(call))
        {
          ROS_ERROR ("Error in ROSjloComm: Update of pose information not psossible!\n");
          return false;
        }
        else if (call.response.error.length() > 0)
        {
          ROS_ERROR ("Error from jlo: %s!\n", call.response.error.c_str());
          return false;
        } 
        
        ROS_INFO ("New Id: %lld (parent %lld)\n", (long long int)call.response.answer.id, (long long int)call.response.answer.parent_id);
        width = 4;
        for(int r = 0; r < width; r++)
        {
          for(int c = 0; c < width; c++)
          {
             printf("%f", call.response.answer.pose[r * width + c]);
          }
          printf("\n");
        }
        printf("\n");

        o->lo_id = call.response.answer.id;
      }
      return true;
    }

    bool call_cop (int table_num)
    { 
      ros::ServiceClient cop_client_ = nh_.serviceClient<vision_srvs::cop_call> ("/tracking/in", true);
      std::vector <std::string> colors;
      colors.push_back ("blue");
      colors.push_back ("black");
      colors.push_back ("green");
      colors.push_back ("red");
      colors.push_back ("white");

      for (unsigned int col = 0; col < colors.size(); col ++)
      {
        /** Create the cop_call msg*/
        vision_srvs::cop_call call;
        call.request.outputtopic = input_cop_topic_;
        call.request.object_classes.push_back (colors[col]);
        call.request.action_type = 0;
        call.request.number_of_objects = tables[table_num].getCurrentInstance ()->objects.size ();
        
        for (unsigned int o_idx = 0; o_idx < tables[table_num].getCurrentInstance ()->objects.size (); o_idx++)
        {
          TableObject *o = tables[table_num].getCurrentInstance ()->objects [o_idx];
          
          // save the indices in our vector of vector of vector so we can find a object
          // in our storage from the positionID (lo_id)
          std::vector<long> idxs (3);
          idxs[0] = table_num;
          idxs[1] = tables[table_num].inst.size()-1;
          idxs[2] = o_idx;
          lo_ids [o->lo_id] = idxs;

          update_prolog_.push_back(o->lo_id);

          vision_msgs::apriori_position pos;
          pos.probability = 1.0;
          pos.positionId = o->lo_id;
          
          call.request.list_of_poses.push_back (pos);
        } 
          
        if(!cop_client_.call(call))
        {
          ROS_INFO("Error calling cop\n");
          return false;
        }
        else
        {
          ROS_INFO("Called cop \n");
        }
      }
      return true;
    }

    bool update_table_instance_objects (int table_num)
    {
      for (unsigned int o_idx = 0; o_idx < tables[table_num].getCurrentInstance ()->objects.size (); o_idx++)
        {
          //TableObject *o = tables[table_num].getCurrentInstance ()->objects [o_idx];
          // save the indices in our vector of vector of vector so we can find a object
          // in our storage from the positionID (lo_id)
          std::vector<long> idxs (3);
          idxs[0] = table_num;
          idxs[1] = tables[table_num].inst.size()-1;
          idxs[2] = o_idx;
          //lo_ids [o->lo_id] = idxs;
          ROS_INFO("Pushing --------- lo_id: %lld, table_num: %ld, inst num: %ld, o_idx: %ld", lo_id_tmp_, idxs[0], idxs[1], idxs[2]);
          lo_ids [lo_id_tmp_] = idxs;
          update_prolog_.push_back(lo_id_tmp_);
          lo_id_tmp_++;
        } 
      return true;
    }


    /*!
     * \brief loads plugins
    */
    void 
      load_plugins ()
    {
      cl = new pluginlib::ClassLoader <CloudAlgo> ("cloud_algos", "CloudAlgo");

      ROS_INFO("ClassLoader instantiated");
      std::vector<std::string> plugins = cl->getDeclaredClasses();
        
      for (std::vector<std::string>::iterator it = plugins.begin(); it != plugins.end() ; ++it)
      {
        ROS_INFO("%s is in package %s and is of type %s", it->c_str(), cl->getClassPackage(*it).c_str(), cl->getClassType(*it).c_str());
        ROS_INFO("It does \"%s\"", cl->getClassDescription(*it).c_str());
      }
 
      try
      {
        cl->loadLibraryForClass("RotationalEstimation");
        ROS_INFO("Loaded library with plugin RotationalEstimation inside");
      }
      catch(pluginlib::PluginlibException &ex)
      {
        ROS_INFO("Failed to load library with plugin RotationalEstimation inside. Exception: %s", ex.what());
      }
    
      if (cl->isClassLoaded("RotationalEstimation"))
      {
        ROS_INFO("Can create RotationalEstimation");
        alg_rot_est = cl->createClassInstance("RotationalEstimation");
        alg_rot_est->init (nh_);
      }
      else ROS_INFO("RotationalEstimation Class not loaded");
    }
    
    /*!
     * \brief employs different reconstruction algorithms for the current set of point clusters on table specified by table_num
    */
    void
      reconstruct_table_objects (int table_num)
    {
      //return;
      
      if (cl->isClassLoaded("RotationalEstimation"))
      {
        ROS_INFO("RotationalEstimation is loaded");
        // use the class
      }
      else 
      {
        ROS_INFO("RotationalEstimation Class not loaded");
        return;
      }

      Table &t = tables[table_num];
//      table_reconstruct_clusters_client_ = nh_.serviceClient<ias_table_srvs::ias_reconstruct_object> ("ias_reconstruct_object", true);
      //if (table_reconstruct_clusters_client_.exists ())
      {
        ROS_WARN ("[reconstruct_table_objects] Table has %i objects.", (int)t.getCurrentInstance ()->objects.size());
        for (int i = 0; i < (signed int) t.getCurrentInstance ()->objects.size (); i++)
        {
          TableObject* to = t.getCurrentInstance ()->objects.at (i); 
          if (to->point_cluster.points.size () == 0)
          {
            ROS_WARN ("[reconstruct_table_objects] Table object has 0 points.");
            continue;
          }
	  std::vector<std::string> bla = alg_rot_est->pre ();//ocess (sensor_msgs::PointCloudConstPtr (&to->point_cluster));
          std::cerr << "[reconstruct_table_objects] Calling with a PCD with " << to->point_cluster.points.size () << " points." << std::endl;
	  std::string blastring = ((RotationalEstimation*)alg_rot_est)->process (sensor_msgs::PointCloudConstPtr (&to->point_cluster, dummy_deleter()));//ocess (sensor_msgs::PointCloudConstPtr (&to->point_cluster));
          ROS_INFO("got response: %s", blastring.c_str ());

          break; 


///////////////////////
//          // formulate a request
//          ias_table_srvs::ias_reconstruct_object::Request req;
//          req.cloud_in = to->point_cluster;
//          req.prob_thresh = 0.99;
//          req.ransac_thresh = 0.05;
//          req.angle_thresh = 10;
//          req.interesting_types = ias_table_msgs::TableObjectReconstructed::PLANE;
//          
//          //call service
//          ias_table_srvs::ias_reconstruct_object::Response resp;
//          table_reconstruct_clusters_client_.call (req, resp); 
//          
//          // update our table object with detected shapes
//          if (resp.objects.size () > 0)
//          {
//            ias_table_msgs::TableObjectReconstructed rto = resp.objects.at(0);
//            to->type = rto.type;
//            to->coeffs = rto.coefficients;
//            to->score = rto.score;
//            //to->center;
//            //to->triangles;
//            //TODO: what if we saw multiple things in one cluster??
//          }
///////////////////////
        }
      }
    }

    // incoming data...
    void
      table_cb (const ias_table_msgs::TableWithObjects::ConstPtr& table)
    {
      int table_found = -1;
      ROS_INFO ("Looking for table in list of known tables.");
      for (int i = 0; i < (signed int) tables.size (); i++)
      {
        if (compare_table (tables[i], table))
        { 
          // found same table earlier.. so we append a new table instance measurement
          ROS_INFO ("Table found.");
          update_table (i, table);
          table_found = i;
          break;
        }
      }
      if (table_found == -1)
      {
        std::vector<double> normal_z(3);
        normal_z[0] = 0.0;
        normal_z[1] = 0.0;
        normal_z[2] = 1.0;
        double area = cloud_geometry::areas::compute2DPolygonalArea (table->table, normal_z);
        if (area < 0.15)
        {
          ROS_INFO ("Table area too small.");
          return;
        }

        ROS_INFO ("Not found. Creating new table.");
        Table t;
        t.center.x = table->table_min.x + ((table->table_max.x - table->table_min.x) / 2.0);
        t.center.y = table->table_min.y + ((table->table_max.y - table->table_min.y) / 2.0);
        t.center.z = table->table_min.z + ((table->table_max.z - table->table_min.z) / 2.0);
        for (unsigned int i = 0; i < table->table.points.size(); i++)
          t.polygon.points.push_back (table->table.points.at(i));
        
        if (table->table.points.size() < 1)
          ROS_WARN ("Got degenerate polygon.");
        t.color = ((int)(rand()/(RAND_MAX + 1.0)) << 16) +
                  ((int)(rand()/(RAND_MAX + 1.0)) << 8) +
                  ((int)(rand()/(RAND_MAX + 1.0)));
        tables.push_back (t);
        table_found = tables.size () - 1;
        // also append the new (first) table instance measurement.
        update_table (table_found, table);
      }
      
      reconstruct_table_objects (table_found);
      update_table_instance_objects(table_found);
      //update_jlo (table_found);
      //call_cop (table_found);
      print_mem_stats (table_found);
      publish_mem_state (table_found);
    }

    void
      publish_mem_state (int table_num)
    {
      mapping_msgs::PolygonalMap pmap;
      pmap.header.frame_id = global_frame_;

      pmap.chan.resize(1);
      pmap.chan[0].name = "rgb";
      for (unsigned int i = 0; i < tables.size(); i++)
      {
        geometry_msgs::Polygon p = tables[i].polygon;
        pmap.polygons.push_back (p);
        pmap.chan[0].values.push_back (tables[i].color);
      }
      mem_state_pub_.publish (pmap);


      sensor_msgs::PointCloud pc;
      pc.header.frame_id = global_frame_;
      for (unsigned int i = 0; i < tables.size(); i++)
      {
        for (unsigned int ob_i = 0; ob_i < tables[i].getCurrentInstance ()->objects.size(); ob_i++)
          for (unsigned int pc_i = 0; pc_i < tables[i].getCurrentInstance ()->objects[ob_i]->point_cluster.points.size(); pc_i++)
            pc.points.push_back (tables[i].getCurrentInstance ()->objects[ob_i]->point_cluster.points[pc_i]);
      }
      cloud_pub_.publish (pc);
    }

    void 
      print_mem_stats (int table_num)
    {
      std::cerr << "Tables : " << tables.size () << std::endl;
      for (unsigned int t = 0; t < tables.size(); t++)
      {
        double x = tables[t].center.x;
        double y = tables[t].center.y;
        double z = tables[t].center.z;
        std::cerr << "\tTable " << t << " : " << tables[t].inst.size () << " instances. Center = <" << x << "," << y << "," << z << ">." << std::endl;

      }
      std::cerr << "\tCurrent Table : " << table_num << " (" << tables[table_num].getCurrentInstance ()->objects.size () << " objects)" << std::endl;
    }

    bool 
      spin ()
    {
      while (ros::ok())
      {
        ros::spinOnce ();
        if (counter_ > 0)
          return true;
      }
      return true;
    }
};

int main (int argc, char* argv[])
{
  ros::init (argc, argv, "table_memory");

  ros::NodeHandle nh("~");
  TableMemory n(nh);
  ros::spin ();

  return (0);
}
