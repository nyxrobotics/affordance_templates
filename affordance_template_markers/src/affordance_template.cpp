#include <affordance_template_markers/affordance_template.h>

using namespace affordance_template;
using namespace affordance_template_object;
using namespace affordance_template_markers;
using namespace affordance_template_msgs;

AffordanceTemplate::AffordanceTemplate(const ros::NodeHandle nh, 
                                        boost::shared_ptr<interactive_markers::InteractiveMarkerServer> server,  
                                        std::string robot_name, 
                                        std::string template_type,
                                        int id) :
  nh_(nh),
  server_(server),
  robot_name_(robot_name),
  template_type_(template_type),
  id_(id),
  root_object_(""),
  loop_rate_(10.0),
  object_controls_display_on_(true),
  planning_server_(nh, (template_type + "_" + std::to_string(id) + "/plan_action"), boost::bind(&AffordanceTemplate::planRequest, this, _1), false),
  execution_server_(nh, (template_type + "_" + std::to_string(id) + "/execute_action"), boost::bind(&AffordanceTemplate::executeRequest, this, _1), false)
{
  ROS_INFO("AffordanceTemplate::init() -- Done Creating new AffordanceTemplate of type %s for robot: %s", template_type_.c_str(), robot_name_.c_str());
  name_ = template_type_ + ":" + std::to_string(id);

  setupMenuOptions();

  planning_server_.start();
  execution_server_.start();
 
  updateThread_.reset(new boost::thread(boost::bind(&AffordanceTemplate::run, this)));

  // set to false when template gets destroyed, otherwise we can get a dangling pointer
  running_ = true;
  autoplay_display_ = true;
}


AffordanceTemplate::AffordanceTemplate(const ros::NodeHandle nh, 
                                        boost::shared_ptr<interactive_markers::InteractiveMarkerServer> server,  
                                        boost::shared_ptr<affordance_template_markers::RobotInterface> robot_interface,
                                        std::string robot_name, 
                                        std::string template_type,
                                        int id) :
  AffordanceTemplate(nh, server, robot_name, template_type, id)
{
  setRobotInterface(robot_interface);
}


AffordanceTemplate::~AffordanceTemplate() 
{
  updateThread_->join();
}

void AffordanceTemplate::setRobotInterface(boost::shared_ptr<affordance_template_markers::RobotInterface> robot_interface)
{
  robot_interface_ = robot_interface;
}


void AffordanceTemplate::setupMenuOptions() 
{

  waypoint_menu_options_.clear();
  waypoint_menu_options_.push_back(MenuConfig("Change End-Effector Pose", false));
  waypoint_menu_options_.push_back(MenuConfig("Hide Controls", true));
  waypoint_menu_options_.push_back(MenuConfig("Compact View", true));
  waypoint_menu_options_.push_back(MenuConfig("Add Waypoint Before", false));
  waypoint_menu_options_.push_back(MenuConfig("Add Waypoint After", false));
  waypoint_menu_options_.push_back(MenuConfig("Delete Waypoint", false));
  waypoint_menu_options_.push_back(MenuConfig("Move Forward", false));
  waypoint_menu_options_.push_back(MenuConfig("Move Back", false));
  
  object_menu_options_.clear();
  object_menu_options_.push_back(MenuConfig("Add Waypoint Before", false));
  object_menu_options_.push_back(MenuConfig("Add Waypoint After", false));
  object_menu_options_.push_back(MenuConfig("Reset", false));
  object_menu_options_.push_back(MenuConfig("Save", false));
  object_menu_options_.push_back(MenuConfig("Hide Controls", true));
  object_menu_options_.push_back(MenuConfig("Choose Trajectory", false));
  object_menu_options_.push_back(MenuConfig("Plan Test", false));
  object_menu_options_.push_back(MenuConfig("Execute Test", false));
  object_menu_options_.push_back(MenuConfig("Plan and Execute Test", false));
  object_menu_options_.push_back(MenuConfig("Knob Test", false));

  // @seth added 12/28 to start on context menu
  object_menu_options_.push_back(MenuConfig("(Re)Play Plan", false));
  object_menu_options_.push_back(MenuConfig("Loop Animation", true));
  object_menu_options_.push_back(MenuConfig("Autoplay", true));
}

bool AffordanceTemplate::loadFromFile(std::string filename, geometry_msgs::Pose pose, AffordanceTemplateStructure &structure)
{
  at_parser_.loadFromFile(filename, structure);

  // store copies in class, one as backup to reset/restore later
  initial_structure_ = structure;
  structure_ = structure;

  bool append = appendIDToStructure(structure_);
  bool created = createFromStructure(structure_);

  return (append && created);
}

bool AffordanceTemplate::saveToDisk(std::string& filename, const std::string& image, const std::string& key, bool save_scale_updates)
{
  std::string class_type = template_type_;
  if (filename.empty())
    filename = template_type_ + ".json";
  else
  {
    std::vector<std::string> keys;
    boost::split(keys, filename, boost::is_any_of("."));
    if (keys.size())
      class_type = keys.front();
  }

  std::string root = ros::package::getPath("affordance_template_library");
  if (root.empty())
      return false;
  root += "/templates";
  std::string output_path = root + "/" + filename;

  // if filename given is actually directory, return
  if (boost::filesystem::is_directory(output_path))
  {
    ROS_ERROR("[AffordanceTemplate::saveToDisk] error formatting filename!!");
    return false;
  }

  ROS_INFO("[AffordanceTemplate::saveToDisk] writing template to file: %s", output_path.c_str());

  if (!boost::filesystem::exists(output_path))
    ROS_WARN("[AffordanceTemplate::saveToDisk] no file found with name: %s. cannot create backup.", filename.c_str());
  else
  {
    // count how many bak files we have for this particular template type
    boost::filesystem::recursive_directory_iterator dir_it(root);
    boost::filesystem::recursive_directory_iterator end_it;
    int bak_counter = 0;
    while (dir_it != end_it)
    {

      if (std::string(dir_it->path().string()).find(".bak") != std::string::npos
          && std::string(dir_it->path().string()).find(class_type) != std::string::npos)
        ++bak_counter;
      ++dir_it;
    }

    // copy current class_type.json into .bak
    std::string bak_path = "";
    if (bak_counter > 0)
    {
      ROS_INFO("[AffordanceTemplate::saveToDisk] creating backup file: %s.bak%d", filename.c_str(), bak_counter);
      bak_path = output_path + ".bak" + std::to_string(bak_counter);
    }
    else
    {
      ROS_INFO("[AffordanceTemplate::saveToDisk] creating backup file: %s.bak", filename.c_str());
      bak_path = output_path + ".bak";
    }
    boost::filesystem::copy_file(output_path, bak_path, boost::filesystem::copy_option::overwrite_if_exists);
  }

  // set structure key as the new key name
  std::vector<std::string> keys;
  boost::split(keys, key, boost::is_any_of(":"));
  if (keys.size())
    structure_.name = keys.front();

  return at_parser_.saveToFile(output_path, structure_);
}

bool AffordanceTemplate::appendIDToStructure(AffordanceTemplateStructure &structure) 
{
  structure.name = appendID(structure.name);
  for(auto &obj : structure.display_objects) {
      obj.name = appendID(obj.name);
      if(obj.parent != "") {
        obj.parent = appendID(obj.parent);
      }
  }
  //       for traj in structure['end_effector_trajectory'] :
  //           for ee_group in traj['end_effector_group'] :
  //               for wp in ee_group['end_effector_waypoint'] :
  //                   wp['display_object'] = self.append_id(wp['display_object'])
  return true;
}
 
std::string AffordanceTemplate::createWaypointID(int ee_id, int wp_id)
{
  return std::to_string(ee_id) + "." + std::to_string(wp_id) + ":" + name_;
}

std::string AffordanceTemplate::appendID(std::string s) 
{
  return s + ":" + std::to_string(id_);
} 

bool AffordanceTemplate::addTrajectory(const std::string& trajectory_name) 
{
  affordance_template_object::Trajectory traj;
  traj.name = trajectory_name;
  structure_.ee_trajectories.push_back(traj);
  setTrajectory(trajectory_name);
  setupTrajectoryMenu(structure_, trajectory_name);
  return createFromStructure( structure_, true, false, trajectory_name);
}

bool AffordanceTemplate::getTrajectory(TrajectoryList& traj_list, std::string traj_name, Trajectory& traj) 
{
  for (auto &t: traj_list) {
    if(t.name == traj_name) {
      traj = t;
      return true;
    }
  }
  ROS_ERROR("AffordanceTemplate::getTrajectory() -- could not find %s in list of trajectories", traj_name.c_str());
  return false;
}

bool AffordanceTemplate::getTrajectoryPlan(const std::string& trajectory, const std::string& ee, PlanStatus& plan)
{
  if (plan_status_.find(trajectory) != plan_status_.end()) {
    if (plan_status_[trajectory].find(ee) != plan_status_[trajectory].end()) {
      plan = plan_status_[trajectory][ee];
    } else {
      return false;
    }
  } else {
    return false;
  }
  return true;
}

bool AffordanceTemplate::setTrajectory(const std::string& trajectory_name)
{
  return setCurrentTrajectory( getCurrentStructure().ee_trajectories, trajectory_name);
}

bool AffordanceTemplate::switchTrajectory(const std::string& trajectory_name)
{
  removeAllMarkers();
  if(setTrajectory(trajectory_name)) {
    setupTrajectoryMenu(structure_, trajectory_name);
    if(createFromStructure( structure_, true, false, trajectory_name)) {
      ROS_DEBUG("AffordanceTemplate::switchTrajectory() -- %s succeeded", trajectory_name.c_str());
    } else {
      ROS_ERROR("AffordanceTemplate::switchTrajectory() -- %s failed", trajectory_name.c_str());
      return false;
    }

    // robot_interface_->getPlanner()->resetAnimation(); // @seth added 12/29 FIXME
  }
 return true;
}

void AffordanceTemplate::clearTrajectoryFlags()
{
  waypoint_flags_.clear();
}

bool AffordanceTemplate::getWaypointFlags(const std::string& traj, WaypointTrajectoryFlags& flags) {
  if(waypoint_flags_.find(traj)!=waypoint_flags_.end()) {
    flags = waypoint_flags_[traj];
  } else {
    ROS_ERROR("AffordanceTemplate::getWaypointFlags() -- no traj=%s found", traj.c_str());
    return false;
  }
  return true;
}
    
void AffordanceTemplate::setTrajectoryFlags(Trajectory traj) 
{  
  if(waypoint_flags_.find(traj.name) == std::end(waypoint_flags_)) {
    WaypointTrajectoryFlags wp_flags;

    wp_flags.run_backwards = false;
    wp_flags.loop = false;
    wp_flags.auto_execute = false;

    for(auto &ee : traj.ee_waypoint_list) {
      for(size_t idx=0; idx<ee.waypoints.size(); idx++) {
        std::string wp_name = createWaypointID(ee.id,idx);
        wp_flags.controls_on[wp_name] = false;
        wp_flags.compact_view[wp_name] = false;
      }
    }
    waypoint_flags_[traj.name] = wp_flags;
  }

  // if(plan_status_.find(traj.name) == std::end(plan_status_)) {
  //   plan_status_[traj.name][ee].direct     = false;
  //   plan_status_[traj.name][ee].plan_valid = false;
  //   plan_status_[traj.name][ee].exec_valid = false;
  //   plan_status_[traj.name][ee].backwards  = false;
  //   plan_status_[traj.name][ee].sequence_ids.clear();
  //   plan_status_[traj.name][ee].sequence_poses.clear();
  // }

}

bool AffordanceTemplate::isValidTrajectory(Trajectory traj)  
{
  bool valid_trajectory = true;
  for(auto &g: traj.ee_waypoint_list) {
    for(auto &wp: g.waypoints) {
      if(robot_interface_->getEENameMap().find(g.id) == std::end(robot_interface_->getEENameMap())) {
        valid_trajectory = false;
        ROS_DEBUG("AffordanceTemplate::is_valid_trajectory() -- can't find ee ID: %d", g.id);
        return valid_trajectory;
      }
   }
 }
 return valid_trajectory;
} 

// set the default (current) traj to the input one.
// if no input request, find the first valid one
bool AffordanceTemplate::setCurrentTrajectory(TrajectoryList traj_list, std::string traj) 
{
  ROS_INFO("[AffordanceTemplate::setCurrentTrajectory] will attempt to set current trajectory to %s", traj.c_str());

  current_trajectory_ = "";
  if ( !traj.empty()) {
    for (auto &t: traj_list) {
      if(t.name == traj) {
        if(isValidTrajectory(t)) {
          current_trajectory_ = t.name;
          ROS_INFO("AffordanceTemplate::setCurrentTrajectory() -- setting current trajectory to: %s", current_trajectory_.c_str());
          break;
        } else {
          ROS_ERROR("[AffordanceTemplate::setCurrentTrajectory] -- \'%s\' not a valid trajectory", t.name.c_str());
        }
      }
    }
  } else {
    ROS_ERROR("[AffordanceTemplate::setCurrentTrajectory] -- input trajectory is empty");
  }

  // get the first valid trajectory
  if ( current_trajectory_.empty()) {
    for (auto &t: traj_list) {
      if(isValidTrajectory(t)) {
        current_trajectory_ = t.name;
        if(traj!=current_trajectory_) {
          ROS_WARN("AffordanceTemplate::setCurrentTrajectory() -- \'%s\' not valid, setting current trajectory to: %s", traj.c_str(), current_trajectory_.c_str());
        } else {
          ROS_INFO("AffordanceTemplate::setCurrentTrajectory() -- setting current trajectory to: %s", current_trajectory_.c_str());          
        }
        break;
      } 
    }
  } 
  
  if (current_trajectory_.empty()) // still no valid traj found
  {
    ROS_ERROR("AffordanceTemplate::createDisplayObjectsFromStructure() -- no valid trajectory found");
    return false;
  }
  return true;
}


bool AffordanceTemplate::setTemplatePose(geometry_msgs::PoseStamped ps)
{

  key_ = structure_.name;

    //ps.pose = robot_interface_->getRobotConfig().root_offset;
    //ps.header.frame_id = robot_interface_->getRobotConfig().frame_id;
  frame_store_[key_] = FrameInfo(key_, ps);


  return true;
}
    

bool AffordanceTemplate::setWaypointViewMode(int ee, int wp, bool m)
{
  std::string wp_name = createWaypointID(ee, wp);
  waypoint_flags_[current_trajectory_].compact_view[wp_name] = m;
  ROS_DEBUG("AffordanceTemplate::setWaypointViewMode() -- setting compact_view for [%s] to %d", wp_name.c_str(), (int)m);
  removeAllMarkers();
  createFromStructure(structure_, true, true, current_trajectory_);
  server_->applyChanges();
  return true;
}
    
bool AffordanceTemplate::createFromStructure(AffordanceTemplateStructure structure, bool keep_object_poses, bool keep_waypoint_poses, std::string traj) 
{
  ROS_INFO("AffordanceTemplate::createFromStructure() -- %s", template_type_.c_str());
  if(setCurrentTrajectory(structure.ee_trajectories, traj)) {
    if(!createDisplayObjectsFromStructure(structure, keep_object_poses)) {
      ROS_ERROR("AffordanceTemplate::createFromStructure() -- couldn't createDisplayObjectsFromStructure()");
      return false;
    }
    if(!createWaypointsFromStructure(structure, keep_waypoint_poses)) {
      ROS_ERROR("AffordanceTemplate::createFromStructure() -- couldn't createWaypointsFromStructure()"); 
      return false;
    }
  } else {
    ROS_ERROR("AffordanceTemplate::createFromStructure() -- couldn't set the current trajectory");
    return false;
  }
  ROS_INFO("AffordanceTemplate::createFromStructure() -- done creating %s", template_type_.c_str());

  return true;
}
 
bool AffordanceTemplate::createDisplayObjectsFromStructure(affordance_template_object::AffordanceTemplateStructure structure, bool keep_poses) {

  int idx = 0;
  key_ = structure.name;

  {
    geometry_msgs::PoseStamped ps;
    ps.pose = robot_interface_->getRobotConfig().root_offset;
    ps.header.frame_id = robot_interface_->getRobotConfig().frame_id;

    if(frame_store_.find(key_) == frame_store_.end()) {
      frame_store_[key_] = FrameInfo(key_, ps);
    }
  }

  for(auto &obj: structure.display_objects) {

    ROS_INFO("AffordanceTemplate::createDisplayObjectsFromStructure() creating Display Object: %s", obj.name.c_str());

    setupObjectMenu(structure, obj);

    root_frame_ = template_type_;
    if(obj.parent != "") {
      root_frame_ = obj.parent;
    } else {
      root_frame_ = key_;
    }

    // set scale factor if not already set
    if(object_scale_factor_.find(obj.name) == std::end(object_scale_factor_)) {
      object_scale_factor_[obj.name] = 1.0;
      ee_scale_factor_[obj.name] = 1.0;
      ROS_DEBUG("[AffordanceTemplate::createDisplayObjectsFromStructure] setting scale factor for %s to default 1.0", obj.name.c_str());
    } //TODO deubg??
    // else {
    //   ROS_WARN("huh %s", obj.name.c_str());
    // }

    //   object_scale_factor_[obj.name] = 1.0;
    //   ee_scale_factor_[obj.name] = 1.0;

    visualization_msgs::InteractiveMarker int_marker;
    int_marker.header.frame_id = root_frame_;
    int_marker.header.stamp = ros::Time(0);
    int_marker.name = obj.name;
    int_marker.description = obj.name;
    int_marker.scale = obj.controls.scale*object_scale_factor_[obj.name];

    visualization_msgs::InteractiveMarkerControl control;
    control.always_visible = true;
    control.interaction_mode = visualization_msgs::InteractiveMarkerControl::BUTTON;    

    visualization_msgs::Marker marker;
    marker.ns = obj.name;
    marker.id = idx++;
  
    if(!keep_poses || !hasObjectFrame(obj.name) ) {
      geometry_msgs::PoseStamped ps;
      ps.header.frame_id = root_frame_;
      ps.pose = originToPoseMsg(obj.origin);
      frame_store_[obj.name] = FrameInfo(obj.name, ps);
      int_marker.pose = ps.pose;
    } else {
      int_marker.pose = frame_store_[obj.name].second.pose;
      if(obj.parent != "") {
        // what is this? confused.... -SH
        int_marker.pose.position.x /= object_scale_factor_[obj.name];
        int_marker.pose.position.y /= object_scale_factor_[obj.name];
        int_marker.pose.position.z /= object_scale_factor_[obj.name];
      }
    }

    // adjust for scale factor of parent object
    if(obj.parent != "") {
      int_marker.pose.position.x *= object_scale_factor_[obj.name];
      int_marker.pose.position.y *= object_scale_factor_[obj.name];
      int_marker.pose.position.z *= object_scale_factor_[obj.name];
    }
    frame_store_[obj.name].second.pose = int_marker.pose;

    if(obj.shape.type == "mesh") {
      marker.type = visualization_msgs::Marker::MESH_RESOURCE;
      marker.mesh_resource = obj.shape.mesh;
      marker.scale.x = obj.shape.size[0]*object_scale_factor_[obj.name];
      marker.scale.y = obj.shape.size[1]*object_scale_factor_[obj.name];
      marker.scale.z = obj.shape.size[2]*object_scale_factor_[obj.name];
      ROS_DEBUG("Drawing Mesh for object %s : %s (scale=%.3f)", obj.name.c_str(), marker.mesh_resource.c_str(), object_scale_factor_[obj.name]);
    } else if(obj.shape.type == "box") {
      marker.type = visualization_msgs::Marker::CUBE;
      marker.scale.x = obj.shape.size[0]*object_scale_factor_[obj.name];
      marker.scale.y = obj.shape.size[1]*object_scale_factor_[obj.name];
      marker.scale.z = obj.shape.size[2]*object_scale_factor_[obj.name];
    } else if(obj.shape.type == "sphere") {
      marker.type = visualization_msgs::Marker::SPHERE;
      marker.scale.x = obj.shape.size[0]*object_scale_factor_[obj.name];
      marker.scale.y = obj.shape.size[1]*object_scale_factor_[obj.name];
      marker.scale.z = obj.shape.size[2]*object_scale_factor_[obj.name];
    } else if(obj.shape.type == "cylinder") {
      marker.type = visualization_msgs::Marker::CYLINDER;
      marker.scale.x = obj.shape.radius*object_scale_factor_[obj.name];
      marker.scale.y = obj.shape.length*object_scale_factor_[obj.name];      
    }

    control.markers.push_back(marker);

    if(obj.shape.type != "mesh") {
      control.markers[0].color.r = obj.shape.rgba[0];
      control.markers[0].color.g = obj.shape.rgba[1];
      control.markers[0].color.b = obj.shape.rgba[2];
      control.markers[0].color.a = obj.shape.rgba[3];
    } else {
      control.markers[0].mesh_use_embedded_materials = true;
      control.markers[0].color.r = 1.0;
      control.markers[0].color.g = 1.0;
      control.markers[0].color.b = 1.0;
      control.markers[0].color.a = 1.0;
    }

    // double scale = 1.0;
    // if obj in self.object_controls :
    //     scale = self.object_controls[obj]['scale']*self.object_scale_factor[obj]

    //  # int_marker = CreateInteractiveMarker(self.frame_id, obj.name, scale)
    int_marker.controls.push_back(control);
    if(object_controls_display_on_) {
      std::vector<visualization_msgs::InteractiveMarkerControl> dof_controls;
      dof_controls = utils::MarkerHelper::makeCustomDOFControls(obj.controls.translation[0], 
                                                                obj.controls.translation[1], 
                                                                obj.controls.translation[2],
                                                                obj.controls.rotation[0],
                                                                obj.controls.rotation[1],
                                                                obj.controls.rotation[2]);

      for (auto &c: dof_controls) {
        int_marker.controls.push_back(c);
      }
    }   

    //self.marker_pose_offset[obj] = self.pose_from_origin(self.object_origin[obj])

    addInteractiveMarker(int_marker);

    MenuHandleKey key;
    key[obj.name] = {"Hide Controls"};
    if(object_controls_display_on_) {
      marker_menus_[obj.name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::UNCHECKED );
    } else {
      marker_menus_[obj.name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
    }

    // @seth - temp -- there should be a better way to do this
    key[obj.name] = {"Autoplay"};
    if(autoplay_display_) {
      marker_menus_[obj.name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
    } else {
      marker_menus_[obj.name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::UNCHECKED );
    }

    marker_menus_[obj.name].apply( *server_, obj.name );
  }
  server_->applyChanges();

  return true;
}


bool AffordanceTemplate::createWaypointsFromStructure(affordance_template_object::AffordanceTemplateStructure structure, bool keep_poses) 
{
  ROS_DEBUG("AffordanceTemplate::createWaypointsFromStructure() -- trajectory: %s", current_trajectory_.c_str());

  int wp_ids = 0;

  Trajectory traj;
  if(!getTrajectory(structure.ee_trajectories, current_trajectory_, traj)){
    ROS_ERROR("AffordanceTemplate::createWaypointsFromStructure() -- couldn't get the request trajectory");
    return false;
  }

  for(auto &wp_list: traj.ee_waypoint_list) {

    int ee_id = wp_list.id;
    std::map<int, std::string> ee_name_map = robot_interface_->getEENameMap();
    std::string ee_name = ee_name_map[ee_id];

    int wp_id = 0;

    ROS_INFO("AffordanceTemplate::createWaypointsFromStructure() creating Trajectory for Waypoint[%d]: %s", ee_id, ee_name.c_str());

    setTrajectoryFlags(traj);

    for(auto &wp: wp_list.waypoints) {

      std::string wp_name = createWaypointID(ee_id, wp_id);
      ROS_INFO("AffordanceTemplate::createWaypointsFromStructure() creating Waypoint: %s", wp_name.c_str());

      setupWaypointMenu(structure, wp_name);
      geometry_msgs::Pose display_pose = originToPoseMsg(wp.origin);  

      std::string parent_obj = appendID(wp.display_object);
      double parent_scale = object_scale_factor_[parent_obj]*ee_scale_factor_[parent_obj];  

      display_pose.position.x *= parent_scale;
      display_pose.position.y *= parent_scale;
      display_pose.position.z *= parent_scale;

      visualization_msgs::InteractiveMarker int_marker;
      int_marker.header.frame_id = parent_obj;
      int_marker.header.stamp = ros::Time(0);
      int_marker.name = wp_name;
      int_marker.description = wp_name;
      int_marker.scale = wp.controls.scale;

    
      if(!keep_poses || !hasWaypointFrame(wp_name) ) {
        //store base WP pose to publish by TF
        geometry_msgs::PoseStamped ps;
        ps.header.frame_id = parent_obj;
        ps.pose = display_pose;
        frame_store_[wp_name] = FrameInfo(wp_name, ps);
        int_marker.pose = ps.pose;
      } else {
        int_marker.pose = frame_store_[wp_name].second.pose;
        // if(parent_obj != "") {
        //   int_marker.pose.position.x /= object_scale_factor_[obj.name];
        //   int_marker.pose.position.y /= object_scale_factor_[obj.name];
        //   int_marker.pose.position.z /= object_scale_factor_[obj.name];
        // }
      }

      visualization_msgs::InteractiveMarkerControl menu_control;
      menu_control.interaction_mode = visualization_msgs::InteractiveMarkerControl::BUTTON;    
      menu_control.always_visible = true;
      
      MenuHandleKey key;
      if(waypoint_flags_[current_trajectory_].run_backwards) {
        key[wp_name] = {"Compute Backwards Path"};
        marker_menus_[wp_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
      }  
      if(waypoint_flags_[current_trajectory_].auto_execute) {
        key[wp_name] = {"Execute On Move"};
        marker_menus_[wp_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
      }  
      if(waypoint_flags_[current_trajectory_].loop) {
        key[wp_name] = {"Loop Path"};
        marker_menus_[wp_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
      }  
      if(waypoint_flags_[current_trajectory_].controls_on[wp_name]) {
        key[wp_name] = {"Hide Controls"};
        marker_menus_[wp_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::UNCHECKED );
      }  else {
        key[wp_name] = {"Hide Controls"};
        marker_menus_[wp_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
      }
      if(waypoint_flags_[current_trajectory_].compact_view[wp_name]) {
        key[wp_name] = {"Compact View"};
        marker_menus_[wp_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
      }  else {
        key[wp_name] = {"Compact View"};
        marker_menus_[wp_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::UNCHECKED );
      }

      std::string ee_pose_name;;
      try {
        ee_pose_name = robot_interface_->getEEPoseNameMap(ee_name)[wp.ee_pose];
      } catch(...) {
        ee_pose_name = "current";
      }
      ROS_INFO("AffordanceTemplate::createWaypointsFromStructure()   ee_pose_name[%d]: %s", wp.ee_pose, ee_pose_name.c_str());
               
      visualization_msgs::MarkerArray markers;
      end_effector_helper::EndEffectorHelperConstPtr ee_link_data;
      if(robot_interface_->getEELinkData(ee_name, ee_link_data)) { 
        if(!ee_link_data->getMarkersForPose(ee_pose_name, markers)) {
          ROS_ERROR("AffordanceTemplate::createWaypointsFromStructure() -- problem getting pose markers for EE %s, pose: %s", ee_name.c_str(), ee_pose_name.c_str());
          return false;
        } 
      } else {
        ROS_ERROR("AffordanceTemplate::createWaypointsFromStructure() -- no link data for EE %s", ee_name.c_str());
        return false;        
      }

      tf::Transform wpTee, eeTtf;

      // std::cout << "getManipulatorOffsetPose() =" << std::endl;       
      // std::cout << robot_interface_->getManipulatorOffsetPose(ee_name) << std::endl;
      // std::cout << "getToolOffsetPose() =" << std::endl;       
      // std::cout << robot_interface_->getToolOffsetPose(ee_name) << std::endl;

      geometry_msgs::PoseStamped ee_ps;
      ee_ps.header.frame_id = wp_name;
      ee_ps.pose = robot_interface_->getManipulatorOffsetPose(ee_name);
      std::string ee_frame_name = wp_name + "/ee";
      frame_store_[ee_frame_name] = FrameInfo(ee_frame_name, ee_ps);

      geometry_msgs::PoseStamped tf_ps;
      tf_ps.header.frame_id = ee_frame_name;
      tf_ps.pose = robot_interface_->getToolOffsetPose(ee_name);
      std::string tf_frame_name = wp_name + "/tf";
      frame_store_[tf_frame_name] = FrameInfo(tf_frame_name, tf_ps);
    
      try {
        tf::poseMsgToTF(robot_interface_->getManipulatorOffsetPose(ee_name),wpTee);
        tf::poseMsgToTF(robot_interface_->getToolOffsetPose(ee_name),eeTtf);
      } catch(...) {
        ROS_ERROR("AffordanceTemplate::createWaypointsFromStructure() -- error getting transforms for %s", ee_name.c_str());
      }

      if(waypoint_flags_[current_trajectory_].compact_view[wp_name]) {
        int N = getNumWaypoints(structure, current_trajectory_, ee_id);
        ROS_DEBUG("AffordanceTemplate::createWaypointsFromStructure() -- displaying %s in COMPACT mode", wp_name.c_str());
        visualization_msgs::Marker m;
        m.header.frame_id = ee_frame_name;
        m.ns = name_;
        m.type = visualization_msgs::Marker::SPHERE;
        m.scale.x = 0.02;
        m.scale.y = 0.02;
        m.scale.z = 0.02;
        m.color.r = 0.5;
        m.color.g = 0.0;
        m.color.b = wp_id/(N-1.0);
        m.color.a = 0.5;
        m.pose.orientation.w = 1;
        menu_control.markers.push_back( m );
      } else {
        ROS_DEBUG("AffordanceTemplate::createWaypointsFromStructure() -- displaying %s in FULL mode", wp_name.c_str());
        for(auto &m: markers.markers) {
          visualization_msgs::Marker ee_m = m;
          ee_m.header.frame_id = tf_frame_name;
          ee_m.ns = name_;
          ee_m.pose = m.pose;
          menu_control.markers.push_back( ee_m );
        }
        // scale = 1.0
        // if wp in self.waypoint_controls[trajectory] :
        //     scale = self.waypoint_controls[trajectory][wp]['scale']
      }

      int_marker.controls.push_back(menu_control);
      if(waypoint_flags_[current_trajectory_].controls_on[wp_name]) {
        std::vector<visualization_msgs::InteractiveMarkerControl> dof_controls;
        dof_controls = utils::MarkerHelper::makeCustomDOFControls(wp.controls.translation[0], 
                                                                  wp.controls.translation[1], 
                                                                  wp.controls.translation[2],
                                                                  wp.controls.rotation[0],
                                                                  wp.controls.rotation[1],
                                                                  wp.controls.rotation[2]);

        for (auto &c: dof_controls) {
          int_marker.controls.push_back(c);
        }
      }

      // std::cout << int_marker << std::endl;
      addInteractiveMarker(int_marker);

      marker_menus_[wp_name].apply( *server_, wp_name );
      server_->applyChanges();          
 
      wp_id++;
    }
  }
  ROS_DEBUG("AffordanceTemplate::createWaypointsFromStructure() -- done");
 
  return true;
}


bool AffordanceTemplate::insertWaypointInList(affordance_template_object::EndEffectorWaypoint wp, int id, affordance_template_object::EndEffectorWaypointList &wp_list) {
  
  wp_list.waypoints.insert(wp_list.waypoints.begin()+id, wp);

  std::string wp_name_1, wp_name_2; 
  for(size_t k=wp_list.waypoints.size()-1; (int)k>=(int)id; --k) {

    wp_name_1 = createWaypointID(wp_list.id, k);
    wp_name_2 = createWaypointID(wp_list.id, k+1);

    ROS_WARN("ID=%d, k=%d -- moving \'%s\' data to \'%s\'", id, (int)k, wp_name_1.c_str(), wp_name_2.c_str());
    waypoint_flags_[current_trajectory_].controls_on[wp_name_2] = waypoint_flags_[current_trajectory_].controls_on[wp_name_1];
    waypoint_flags_[current_trajectory_].compact_view[wp_name_2] = waypoint_flags_[current_trajectory_].compact_view[wp_name_1];
  }
  return true;
}


void AffordanceTemplate::setupObjectMenu(AffordanceTemplateStructure structure, DisplayObject obj)
{
  for(auto& o : object_menu_options_) 
  {
    if(o.first == "Choose Trajectory") 
      setupTrajectoryMenu(structure, obj.name);
    else if (o.first.find("Add Waypoint") != std::string::npos)
      setupAddWaypointMenuItem(structure, obj.name, o.first);
    else 
      setupSimpleMenuItem(structure, obj.name, o.first, o.second);
  }
}

void AffordanceTemplate::setupWaypointMenu(AffordanceTemplateStructure structure, std::string name)
{
  for(auto& o : waypoint_menu_options_) 
  {
    if(o.first == "Change End-Effector Pose")
      setupEndEffectorPoseMenu(name);
    else
      setupSimpleMenuItem(structure, name, o.first, o.second);
  }
}

void AffordanceTemplate::setupSimpleMenuItem(AffordanceTemplateStructure structure, const std::string& name, const std::string& menu_text, bool has_check_box)
{
  MenuHandleKey key;
  key[name] = {menu_text};  
  group_menu_handles_[key] = marker_menus_[name].insert( menu_text, boost::bind( &AffordanceTemplate::processFeedback, this, _1 ) );  
  if(has_check_box) {
    marker_menus_[name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::UNCHECKED );
  }
}

void AffordanceTemplate::setupAddWaypointMenuItem(AffordanceTemplateStructure structure, std::string name, std::string menu_text)
{
  interactive_markers::MenuHandler::EntryHandle sub_menu_handle = marker_menus_[name].insert( menu_text);

  for(auto& ee: robot_interface_->getEENameMap()) 
  {
    std::string ee_readable = robot_interface_->getReadableEEName(ee.second);

    MenuHandleKey key;
    key[name] = {menu_text, ee.second};
    group_menu_handles_[key] = marker_menus_[name].insert( sub_menu_handle, ee_readable, boost::bind( &AffordanceTemplate::processFeedback, this, _1 ) );   
    
    ROS_DEBUG("[AffordanceTemplate::setupAddWaypointMenuItem] adding submenu text %s to menu item %s", ee_readable.c_str(), menu_text.c_str());
  }
}

void AffordanceTemplate::setupTrajectoryMenu(AffordanceTemplateStructure structure, const std::string& name)
{
  std::string menu_text = "Choose Trajectory";
  interactive_markers::MenuHandler::EntryHandle sub_menu_handle = marker_menus_[name].insert( menu_text );
  for(auto &traj: structure.ee_trajectories) 
  {
    MenuHandleKey key;
    key[name] = {menu_text, traj.name};
    group_menu_handles_[key] = marker_menus_[name].insert( sub_menu_handle, traj.name, boost::bind( &AffordanceTemplate::processFeedback, this, _1 ) );   
  
    if(traj.name == current_trajectory_) 
      marker_menus_[name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
    else
      marker_menus_[name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::UNCHECKED );       
    
    ROS_DEBUG("[AffordanceTemplate::setupTrajectoryMenu] adding submenu text %s to menu item %s", traj.name.c_str(), menu_text.c_str());
  }
}

void AffordanceTemplate::setupEndEffectorPoseMenu(const std::string& name)
{
  std::string menu_text = "Change End-Effector Pose";
  int ee_id = getEEIDfromWaypointName(name);
  std::string ee_name = robot_interface_->getEEName(ee_id);
  interactive_markers::MenuHandler::EntryHandle sub_menu_handle = marker_menus_[name].insert( menu_text );
  for(auto &pose_name: robot_interface_->getEEPoseNames(ee_name)) {
    MenuHandleKey key;
    key[name] = {menu_text, pose_name};
    group_menu_handles_[key] = marker_menus_[name].insert( sub_menu_handle, pose_name, boost::bind( &AffordanceTemplate::processFeedback, this, _1 ) ); 
  }
}

int AffordanceTemplate::getEEIDfromWaypointName(const std::string wp_name) 
{
  std::string delimiter = ".";
  size_t pos = 0;
  std::string token;
  if ((pos = wp_name.find(delimiter)) != std::string::npos) {
    token = wp_name.substr(0, pos);
    return std::stoi(token);
  }
  ROS_ERROR("AffordanceTemplate::getEEIDfromWaypointName() -- could not find EE ID from %s", wp_name.c_str());
  return -1;
}

void AffordanceTemplate::addInteractiveMarker(visualization_msgs::InteractiveMarker m)
{
  ROS_DEBUG("AffordanceTemplate::addInteractiveMarker() -- %s with frame: %s", m.name.c_str(), m.header.frame_id.c_str());
  std::string name = m.name;
  int_markers_[m.name] = m;
  server_->insert(m);
  server_->setCallback(m.name, boost::bind( &AffordanceTemplate::processFeedback, this, _1 ));
}

void AffordanceTemplate::removeInteractiveMarker(std::string marker_name) 
{
  ROS_DEBUG("[AffordanceTemplate::removeInteractiveMarker] removing marker %s", marker_name.c_str());
  server_->erase(marker_name);
  server_->applyChanges();
}

void AffordanceTemplate::removeAllMarkers() 
{
  for(auto &m: int_markers_)
    removeInteractiveMarker(m.first);
  group_menu_handles_.clear();
  int_markers_.clear();
  marker_menus_.clear();
  server_->applyChanges();
}

void AffordanceTemplate::processFeedback(const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback ) 
{
  ROS_INFO("AffordanceTemplate::processFeedback(%s) -- %s", robot_name_.c_str(), feedback->marker_name.c_str());

  interactive_markers::MenuHandler::CheckState state;

  std::string dummy_ee_name = "";
  if(robot_name_=="r2") {
    std::size_t found = current_trajectory_.find("Left");
    if (found!=std::string::npos) {
      dummy_ee_name = "left_hand";
    } else {
      dummy_ee_name = "right_hand";
    }
  } else {
    dummy_ee_name = "gripper";
  }

  // set up key maps for easy comparison to menu handler ID
  MenuHandleKey wp_before_key;
  MenuHandleKey wp_after_key;
  MenuHandleKey reset_key;
  MenuHandleKey save_key;
  MenuHandleKey delete_key;
  MenuHandleKey hide_controls_key;
  MenuHandleKey plan_test_key;
  MenuHandleKey execute_test_key;
  MenuHandleKey plan_and_execute_test_key;
  MenuHandleKey knob_test_key;
  MenuHandleKey view_mode_key;
  
  MenuHandleKey play_plan_key; // @seth
  MenuHandleKey loop_key;
  MenuHandleKey autoplay_key;

  wp_before_key[feedback->marker_name] = {"Add Waypoint Before"};
  wp_after_key[feedback->marker_name] = {"Add Waypoint After"};
  reset_key[feedback->marker_name] = {"Reset"};
  save_key[feedback->marker_name] = {"Save"};
  delete_key[feedback->marker_name] = {"Delete Waypoint"};
  hide_controls_key[feedback->marker_name] = {"Hide Controls"};
  view_mode_key[feedback->marker_name] = {"Compact View"};
  plan_test_key[feedback->marker_name] = {"Plan Test"};
  execute_test_key[feedback->marker_name] = {"Execute Test"};
  plan_and_execute_test_key[feedback->marker_name] = {"Plan and Execute Test"};
  knob_test_key[feedback->marker_name] = {"Knob Test"};
  
  play_plan_key[feedback->marker_name] = {"(Re)Play Plan"}; // @seth 
  loop_key[feedback->marker_name] = {"Loop Animation"};
  autoplay_key[feedback->marker_name] = {"Autoplay"};

  if(hasObjectFrame(feedback->marker_name) || hasWaypointFrame(feedback->marker_name)) {
    geometry_msgs::Pose p = feedback->pose;
    if(feedback->header.frame_id != frame_store_[feedback->marker_name].second.header.frame_id) {
      geometry_msgs::PoseStamped ps;
      ps.pose = feedback->pose;
      ps.header = feedback->header;
      tf_listener_.transformPose (frame_store_[feedback->marker_name].second.header.frame_id, ps, ps);
      p = ps.pose;
    }
    ROS_INFO("storing pose for %s", feedback->marker_name.c_str());
    frame_store_[feedback->marker_name].second.pose = p;
  }
  
  switch ( feedback->event_type ) {

    case visualization_msgs::InteractiveMarkerFeedback::MOUSE_UP :
    {
      //
      // save the current pose of the display objects if they were selected
      for (auto& d : structure_.display_objects)
      {
        if (feedback->marker_name == d.name)
        {
          ROS_DEBUG("[AffordanceTemplate::processFeedback] saving pose for object %s", feedback->marker_name.c_str());
        
          d.origin.position[0] = feedback->pose.position.x;
          d.origin.position[1] = feedback->pose.position.y;
          d.origin.position[2] = feedback->pose.position.z;

          tf::Quaternion q;
          tf::quaternionMsgToTF(feedback->pose.orientation, q);
          double roll, pitch, yaw;
          tf::Matrix3x3(q).getRPY(roll, pitch, yaw);

          d.origin.orientation[0] = roll;
          d.origin.orientation[1] = pitch;
          d.origin.orientation[2] = yaw;

          break;
        }
      }

      //
      // save the current pose of the ee waypoint back to the structure
      bool found = false;
      for (auto& traj : structure_.ee_trajectories)
      {
        if (traj.name == current_trajectory_)
        {
          // look for the object the user selected in our waypoint list
          for (auto& wp_list: traj.ee_waypoint_list) 
          {
            int wp_id = -1; // init to -1 because we pre-add
            for (auto& wp: wp_list.waypoints) 
            {
              std::string wp_name = createWaypointID(wp_list.id, ++wp_id);
              if (wp_name == feedback->marker_name)
              {
                ROS_DEBUG("[AffordanceTemplate::processFeedback] saving pose for EE waypoint %s", feedback->marker_name.c_str());

                wp.origin.position[0] = feedback->pose.position.x;
                wp.origin.position[1] = feedback->pose.position.y;
                wp.origin.position[2] = feedback->pose.position.z;

                tf::Quaternion q;
                tf::quaternionMsgToTF(feedback->pose.orientation, q);
                double roll, pitch, yaw;
                tf::Matrix3x3(q).getRPY(roll, pitch, yaw);

                wp.origin.orientation[0] = roll;
                wp.origin.orientation[1] = pitch;
                wp.origin.orientation[2] = yaw;

                found = true;
                break;
              }
            }
            if (found)
              break;
          }
        }
        if (found)
          break;
      }

      break;
    }
    case visualization_msgs::InteractiveMarkerFeedback::MENU_SELECT : {
      ROS_INFO("[AffordanceTemplate::processFeedback] %s selected menu entry: %d", feedback->marker_name.c_str(), feedback->menu_entry_id);
      // ROS_DEBUG("AffordanceTemplate::processFeedback() -- MENU_SELECT");
      // ROS_DEBUG("AffordanceTemplate::processFeedback() --   menu id: %d", feedback->menu_entry_id);
      // ROS_DEBUG("AffordanceTemplate::processFeedback() --   pose: (%.3f, %.3f, %.3f), (%.3f, %.3f, %.3f, %.3f), frame_id: %s", 
      //                                                             feedback->pose.position.x, feedback->pose.position.y, feedback->pose.position.z, 
      //                                                             feedback->pose.orientation.x, feedback->pose.orientation.y, feedback->pose.orientation.z, feedback->pose.orientation.w,
      //                                                             feedback->header.frame_id.c_str());

      // *****
      // FIXME : this may not be the best way to figure out if we're dealing with the object or an EE
      // *****

      // 
      // check for 'Add Waypoint Before' for EE objects
      if (group_menu_handles_.find(wp_before_key) != std::end(group_menu_handles_)) 
      {
        if (group_menu_handles_[wp_before_key] == feedback->menu_entry_id)
        {
          // Trajectory traj;
          // if (getTrajectory(structure_.ee_trajectories, current_trajectory_, traj)) // FIXME - why wouldn't this give us the actual reference we want to alter data? everything is setup to use references but this seems to give us a copy instead
          bool found = false;
          for (auto& traj : structure_.ee_trajectories)
          {
            if (traj.name == current_trajectory_)
            {
              // look for the object the user selected in our waypoint list
              for (auto& wp_list: traj.ee_waypoint_list) 
              {
                int wp_id = -1; // init to -1 because we pre-add
                for (auto& wp: wp_list.waypoints) 
                {
                  std::string wp_name = createWaypointID(wp_list.id, ++wp_id);
                  if (wp_name == feedback->marker_name)
                  {
                    ROS_DEBUG("[AffordanceTemplate::processFeedback::Add Waypoint Before] for EE waypoint %s", feedback->marker_name.c_str());
                    // TODO and FIXME - this is all just testing insertion
                    affordance_template_object::EndEffectorWaypoint eewp;
                    eewp.ee_pose = 1;
                    eewp.display_object = "test_before_object";
                    eewp.origin.position[0] = eewp.origin.position[1] = eewp.origin.position[2] = 1.0;
                    eewp.origin.orientation[0] = eewp.origin.orientation[1] = eewp.origin.orientation[2] = 0.0;
                    eewp.controls.translation[0] = eewp.controls.translation[1] = eewp.controls.translation[2] = true;
                    eewp.controls.rotation[0] = eewp.controls.rotation[1] = eewp.controls.rotation[2] = true;
                    eewp.controls.scale = 1.0;
                    wp_list.waypoints.insert(wp_list.waypoints.begin(), eewp);

                    found = true;
                    break;
                  }
                }

                if (found) // already found the object - no reason to continue the for loop
                  break;
              }
            }

            if (found) // already found the object - no reason to continue the for loop
              break;
          }
        }
      }

      // 
      // check for 'Add Waypoint Before' for AT object (wheel, door, etc) 
      for (auto& ee: robot_interface_->getEENameMap()) 
      {
        bool found = false;  
        MenuHandleKey key;
        key[feedback->marker_name] = {"Add Waypoint Before", ee.second};
        if (group_menu_handles_.find(key) != std::end(group_menu_handles_)) 
        {
          if (group_menu_handles_[key] == feedback->menu_entry_id)
          {
            // Trajectory traj; 
            // if (getTrajectory(structure_.ee_trajectories, current_trajectory_, traj)) // FIXME - why wouldn't this give us the actual reference we want to alter data? everything is setup to use references but this seems to give us a copy instead
            for (auto& traj : structure_.ee_trajectories)
            {
              if (traj.name == current_trajectory_)
              {
                for (auto& wp_list : traj.ee_waypoint_list) // go through our list of EE waypoints - match based on EE ID
                {
                  if (wp_list.id == robot_interface_->getEEID(ee.second))
                  {
                    ROS_DEBUG("[AffordanceTemplate::processFeedback::Add Waypoint Before] for trajectory: %s and end effector: %s", current_trajectory_.c_str(), robot_interface_->getReadableEEName(ee.second).c_str());
                    // TODO and FIXME - this is all just testing insertion
                    affordance_template_object::EndEffectorWaypoint wp;
                    wp.ee_pose = 1; 
                    wp.display_object = "test_before_object";
                    wp.origin.position[0] = wp.origin.position[1] = wp.origin.position[2] = 1.0;
                    wp.origin.orientation[0] = wp.origin.orientation[1] = wp.origin.orientation[2] = 0.0;
                    wp.controls.translation[0] = wp.controls.translation[1] = wp.controls.translation[2] = true;
                    wp.controls.rotation[0] = wp.controls.rotation[1] = wp.controls.rotation[2] = true;
                    wp.controls.scale = 1.0;
                    wp_list.waypoints.insert(wp_list.waypoints.begin(), wp);

                    found = true;
                    break;
                  }
                }

                if (found) // already found the object - no reason to continue the for loop
                  break;
              }
            }
          }
        }

        if (found) // already found the object - no reason to continue the for loop
          break;
      }

      // check for 'Add Waypoint After' for EE objects
      if (group_menu_handles_.find(wp_after_key) != std::end(group_menu_handles_)) 
      {
        if (group_menu_handles_[wp_after_key] == feedback->menu_entry_id)
        {
          // Trajectory traj;
          // if (getTrajectory(structure_.ee_trajectories, current_trajectory_, traj)) // FIXME - why wouldn't this give us the actual reference we want to alter data? everything is setup to use references but this seems to give us a copy instead
          bool found = false;
          for (auto& traj : structure_.ee_trajectories)
          {
            if (traj.name == current_trajectory_)
            {
              // look for the object the user selected in our waypoint list
              for (auto& wp_list: traj.ee_waypoint_list) 
              {
                int wp_id = -1; // init to -1 because we pre-add
                for (auto& wp: wp_list.waypoints) 
                {
                  std::string wp_name = createWaypointID(wp_list.id, ++wp_id);
                  if (wp_name == feedback->marker_name)
                  {
                    ROS_WARN("[AffordanceTemplate::processFeedback::Add Waypoint After] for EE waypoint: %s", feedback->marker_name.c_str());
                    ROS_WARN("[AffordanceTemplate::processFeedback::Add Waypoint After] -- display object: %s",  appendID(wp.display_object).c_str());
                    ROS_WARN("[AffordanceTemplate::processFeedback::Add Waypoint After] -- ee_pose: %d", wp.ee_pose);

                    affordance_template_object::EndEffectorWaypoint eewp, wp_next;
                    eewp.ee_pose = wp.ee_pose;
                    eewp.display_object = wp.display_object;//appendID(wp.display_object);
 
                    eewp.origin = wp.origin;
                    
                    if(wp_id < wp_list.waypoints.size()-1) {
                      ROS_WARN("averging position with wp %d", wp_id+1);
                      wp_next = wp_list.waypoints[wp_id+1];
                      eewp.origin.position[0] = (wp_next.origin.position[0] +  wp.origin.position[0])/2.0;
                      eewp.origin.position[1] = (wp_next.origin.position[1] +  wp.origin.position[1])/2.0;
                      eewp.origin.position[2] = (wp_next.origin.position[2] +  wp.origin.position[2])/2.0;
                    } else {
                      eewp.origin.position[0] = wp.origin.position[0] - 0.025;
                      eewp.origin.position[1] = wp.origin.position[1] - 0.025;
                      eewp.origin.position[2] = wp.origin.position[2] - 0.025;
                    }

                    eewp.controls = wp.controls;
                    // wp_list.waypoints.insert(wp_list.waypoints.begin()+wp_id+1, eewp);
                    insertWaypointInList(eewp, wp_id+1, wp_list);

                    removeAllMarkers();
                    createFromStructure(structure_, true, true, current_trajectory_); 

                    found = true;
                    break;
                  }
                }

                if (found) // already found the object - no reason to continue the for loop
                  break;
              }
            }

            if (found) // already found the object - no reason to continue the for loop
              break;
          }
        }
      }

      // 
      // check for 'Add Waypoint After' for AT object (wheel, door, etc) 
      for (auto& ee: robot_interface_->getEENameMap()) 
      {
        bool found = false;  
        MenuHandleKey key;
        key[feedback->marker_name] = {"Add Waypoint After", ee.second};
        if (group_menu_handles_.find(key) != std::end(group_menu_handles_)) 
        {
          if (group_menu_handles_[key] == feedback->menu_entry_id)
          {
            // Trajectory traj; 
            // if (getTrajectory(structure_.ee_trajectories, current_trajectory_, traj)) // FIXME - why wouldn't this give us the actual reference we want to alter data? everything is setup to use references but this seems to give us a copy instead
            for (auto& traj : structure_.ee_trajectories)
            {
              if (traj.name == current_trajectory_)
              {
                for (auto& wp_list : traj.ee_waypoint_list) // go through our list of EE waypoints - match based on EE ID
                {
                  if (wp_list.id == robot_interface_->getEEID(ee.second))
                  {

                    ROS_WARN("[AffordanceTemplate::processFeedback::Add Waypoint After] for trajectory: %s and end effector: %s", current_trajectory_.c_str(), robot_interface_->getReadableEEName(ee.second).c_str());
                    ROS_WARN("[AffordanceTemplate::processFeedback::Add Waypoint After] -- display object: %s",  feedback->marker_name.c_str());

                    affordance_template_object::EndEffectorWaypoint wp, eewp;
 
                    if(wp_list.waypoints.size() > 0) {
                      wp = wp_list.waypoints[(int)(wp_list.waypoints.size())-1];
                      eewp = wp;
                    } else {
                      wp.ee_pose = 0;
                      wp.origin.orientation[3] = 1.0;
                    }
                    ROS_WARN("[AffordanceTemplate::processFeedback::Add Waypoint After] -- ee_pose: %d", wp.ee_pose);

                    std::vector<std::string> keys;
                    boost::split(keys, feedback->marker_name, boost::is_any_of(":"));
                    eewp.display_object = keys[0];

                    // tweak pos a bit 
                    eewp.origin.position[0] = wp.origin.position[0] - 0.025;
                    eewp.origin.position[1] = wp.origin.position[1] - 0.025;
                    eewp.origin.position[2] = wp.origin.position[2] - 0.025;
                    
                    eewp.controls = wp.controls;
                    //wp_list.waypoints.push_back(eewp);
                    insertWaypointInList(eewp, wp_list.waypoints.size(), wp_list);

                    removeAllMarkers();
                    createFromStructure(structure_, true, true, current_trajectory_); 

                    // ROS_DEBUG("[AffordanceTemplate::processFeedback::Add Waypoint After] for trajectory: %s and end effector: %s", current_trajectory_.c_str(), robot_interface_->getReadableEEName(ee.second).c_str());
                    // // TODO and FIXME - this is all just testing insertion
                    // affordance_template_object::EndEffectorWaypoint wp;
                    // wp.ee_pose = 1; 
                    // wp.display_object = wp.display_object;
                    // wp.origin.position[0] = wp.origin.position[1] = wp.origin.position[2] = 1.0;
                    // wp.origin.orientation[0] = wp.origin.orientation[1] = wp.origin.orientation[2] = 0.0;
                    // wp.controls.translation[0] = wp.controls.translation[1] = wp.controls.translation[2] = true;
                    // wp.controls.rotation[0] = wp.controls.rotation[1] = wp.controls.rotation[2] = true;
                    // wp.controls.scale = 1.0;
                    // wp_list.waypoints.push_back(wp);

                    // removeAllMarkers();
                    // createFromStructure(structure_, true, true, current_trajectory_); 

                    found = true;
                    break;
                  }
                }

                if (found) // already found the object - no reason to continue the for loop
                  break;
              }
            }
          }
        }

        if (found) // already found the object - no reason to continue the for loop
          break;
      }

      // 
      // delete waypoint
      if (group_menu_handles_.find(delete_key) != group_menu_handles_.end())
      {
        if (group_menu_handles_[delete_key] == feedback->menu_entry_id)
        {
          bool found = false;
          ROS_DEBUG("[AffordanceTemplate::processFeedback::Delete Waypoint] deleting waypoint: %s", feedback->marker_name.c_str());
          for (auto& traj : structure_.ee_trajectories)
          {
            if (traj.name == current_trajectory_)
            {
              // look for the object the user selected in our waypoint list
              for (auto& wp_list: traj.ee_waypoint_list) 
              {
                int wp_id = -1; // init to -1 because we pre-add
                for (auto& wp: wp_list.waypoints) 
                {
                  std::string wp_name = createWaypointID(wp_list.id, ++wp_id);
                  if (wp_name == feedback->marker_name)
                  {
                    if (wp_list.waypoints.size() == 1)
                      wp_list.waypoints.clear();
                    else
                      wp_list.waypoints.erase(wp_list.waypoints.begin() + wp_id);

                    found = true;

                    //FIXME:: is this the best way to handle methods like these?? 
                    //        should these be called at the end of the processFeedback
                    //        or should we be using server->apply() instead??
                    removeAllMarkers();
                    createFromStructure(structure_, true, false, current_trajectory_); 
                    break;
                  }
                }
                if (found)
                  break;
              }
            }
            if (found)
              break;
          }
        }
      }

      // reset AT
      if(group_menu_handles_.find(reset_key) != std::end(group_menu_handles_)) {
        if(group_menu_handles_[reset_key] == feedback->menu_entry_id) {
          ROS_INFO("AffordanceTemplate::processFeedback::Reset] resetting current structure to the inital structure.");
          structure_ = initial_structure_;      
          appendIDToStructure(structure_);
          removeAllMarkers();
          clearTrajectoryFlags();
          createFromStructure(structure_, false, false, current_trajectory_); //FIXME:: make sure current_traj is i ORIGINAL list of traj
        }
      }

      // save AT to file
      if (group_menu_handles_.find(save_key) != group_menu_handles_.end()) 
      {
        if (group_menu_handles_[save_key] == feedback->menu_entry_id) 
        {
          ROS_DEBUG("[AffordanceTemplate::processFeedback::Save] saving file");
          std::vector<std::string> keys;
          boost::split(keys, structure_.filename, boost::is_any_of("/"));
          if (keys.size())
            saveToDisk(keys.back(), structure_.image, feedback->marker_name, true);
          else
            ROS_ERROR("[AffordanceTemplate::processFeedback::Save] invalid filename: %s", structure_.filename.c_str());
        }
      }

      // toggle controls
      if(group_menu_handles_.find(hide_controls_key) != std::end(group_menu_handles_)) {
        if(group_menu_handles_[hide_controls_key] == feedback->menu_entry_id) {
          ROS_INFO("AffordanceTemplate::processFeedback() --   CONTROLS TOGGLE");
          if(marker_menus_[feedback->marker_name].getCheckState( feedback->menu_entry_id, state ) ) {
            if(state == interactive_markers::MenuHandler::CHECKED) {
              marker_menus_[feedback->marker_name].setCheckState( feedback->menu_entry_id, interactive_markers::MenuHandler::CHECKED );
              if(isObject(feedback->marker_name)) {
                object_controls_display_on_ = true;
              } else {
                waypoint_flags_[current_trajectory_].controls_on[feedback->marker_name] = true;
              }
            } else {
              marker_menus_[feedback->marker_name].setCheckState( feedback->menu_entry_id, interactive_markers::MenuHandler::UNCHECKED );
              if(isObject(feedback->marker_name)) {
                object_controls_display_on_ = false;
              } else {
                waypoint_flags_[current_trajectory_].controls_on[feedback->marker_name] = false;
              }
            }
          }
          removeAllMarkers();
          createFromStructure(structure_, true, true, current_trajectory_);
        }
      }

      // toggle EE compact view 
      if(group_menu_handles_.find(view_mode_key) != std::end(group_menu_handles_)) {
        if(group_menu_handles_[view_mode_key] == feedback->menu_entry_id) {
          ROS_INFO("AffordanceTemplate::processFeedback() --   VIEW MODE TOGGLE");
          if(marker_menus_[feedback->marker_name].getCheckState( feedback->menu_entry_id, state ) ) {
            if(state == interactive_markers::MenuHandler::UNCHECKED) {
              marker_menus_[feedback->marker_name].setCheckState( feedback->menu_entry_id, interactive_markers::MenuHandler::CHECKED );
              if(!isObject(feedback->marker_name)) {
                waypoint_flags_[current_trajectory_].compact_view[feedback->marker_name] = true;
              }
            } else {
              marker_menus_[feedback->marker_name].setCheckState( feedback->menu_entry_id, interactive_markers::MenuHandler::UNCHECKED );
              if(!isObject(feedback->marker_name)) {
                waypoint_flags_[current_trajectory_].compact_view[feedback->marker_name] = false;
              }
            }
          }
          removeAllMarkers();
          createFromStructure(structure_, true, true, current_trajectory_);
        }
      }

      //
      // switch trajectories using the context menu
      for (auto &traj: structure_.ee_trajectories) 
      {
        MenuHandleKey key;
        key[feedback->marker_name] = {"Choose Trajectory", traj.name}; // FIXME -- can this be static like this??
        if (group_menu_handles_.find(key) != group_menu_handles_.end())
        {
          marker_menus_[feedback->marker_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::UNCHECKED);
          if (group_menu_handles_[key] == feedback->menu_entry_id) 
          {
            ROS_DEBUG("[AffordanceTemplate::processFeedback::Choose Trajectory] found matching trajectory name %s", traj.name.c_str());
            setTrajectory(traj.name);
            marker_menus_[feedback->marker_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED);
          }
        }
      }

      //
      // switch EE pose of a WP
      if(isWaypoint(feedback->marker_name)) {
        int ee_id = getEEIDfromWaypointName(feedback->marker_name);
        std::string ee_name = robot_interface_->getEEName(ee_id);
        for(auto &pn : robot_interface_->getEEPoseNames(ee_name)) {
          MenuHandleKey key;
          key[feedback->marker_name] = {"Change End-Effector Pose", pn};
          if (group_menu_handles_.find(key) != std::end(group_menu_handles_)) {
            if (group_menu_handles_[key] == feedback->menu_entry_id) {
              ROS_INFO("AffordanceTemplate::processFeedback() -- changing EE[%s] pose to \'%s\'", ee_name.c_str(), pn.c_str());
              bool found = false;
              for (auto& traj : structure_.ee_trajectories) {
                if (traj.name == current_trajectory_) {
                  // look for the object the user selected in our waypoint list
                  for (auto& wp_list: traj.ee_waypoint_list) {
                    int wp_id = -1; // init to -1 because we pre-add
                    for (auto& wp: wp_list.waypoints) {
                      std::string wp_name = createWaypointID(wp_list.id, ++wp_id);
                      if (wp_name == feedback->marker_name) {
                        wp.ee_pose = robot_interface_->getEEPoseIDMap(ee_name)[pn];
                        found = true;
                        removeAllMarkers();
                        if(!createFromStructure(structure_, true, true)){
                          ROS_ERROR("AffordanceTemplate::processFeedback() -- failed creating structure with new EE pose");
                        }
                        break;
                      }
                    }
                    if(found) break;
                  }
                }
                if(found) break;
              }
            }
          }
        }
      }

      // *************************************************************************************
      // TODO: THESE ARE DUMMY HACK FUNCTIONS TO TEST UNTIL WE GET THE ACTIONLIB STUFF WORKING
      // *************************************************************************************

      if(group_menu_handles_.find(plan_test_key) != std::end(group_menu_handles_)) {
        if(group_menu_handles_[plan_test_key] == feedback->menu_entry_id) {
          ROS_WARN("AffordanceTemplate::processFeedback() --   PLAN");
          std::vector<std::string> ee_names = {dummy_ee_name};
          planPathToWaypoints(ee_names, 1, false, false); 
        }
      }

      if(group_menu_handles_.find(execute_test_key) != std::end(group_menu_handles_)) {
        if(group_menu_handles_[execute_test_key] == feedback->menu_entry_id) {
          ROS_WARN("AffordanceTemplate::processFeedback() --   EXECUTE");
          std::vector<std::string> ee_names = {dummy_ee_name};
          moveToWaypoints(ee_names); 
        }
      }

      if(group_menu_handles_.find(plan_and_execute_test_key) != std::end(group_menu_handles_)) {
        if(group_menu_handles_[plan_and_execute_test_key] == feedback->menu_entry_id) {
          ROS_WARN("AffordanceTemplate::processFeedback() --   PLAN & EXECUTE");
          std::vector<std::string> ee_names = {dummy_ee_name};
          planPathToWaypoints(ee_names, 1, false, false); 
          ros::Duration(.5).sleep();
          moveToWaypoints(ee_names); 
        }
      }

      if(group_menu_handles_.find(knob_test_key) != std::end(group_menu_handles_)) {
        if(group_menu_handles_[knob_test_key] == feedback->menu_entry_id) {
          ROS_WARN("AffordanceTemplate::processFeedback() --   KNOB TEST");
          std::vector<std::string> ee_names = {dummy_ee_name};

          planPathToWaypoints(ee_names, 1, false, false); 
          moveToWaypoints(ee_names); 
          ros::Duration(2).sleep();

          planPathToWaypoints(ee_names, 1, false, false); 
          moveToWaypoints(ee_names); 
          ros::Duration(2).sleep();

          planPathToWaypoints(ee_names, 1, false, false); 
          moveToWaypoints(ee_names); 
          ros::Duration(2).sleep();

          planPathToWaypoints(ee_names, 1, false, false); 
          moveToWaypoints(ee_names); 
          ros::Duration(2).sleep();

        }
      }

      // @seth added 12/28
      if (group_menu_handles_.find(play_plan_key) != std::end(group_menu_handles_))
      {
        if (group_menu_handles_[play_plan_key] == feedback->menu_entry_id)
        {
          ROS_INFO("[AffordanceTemplate::processFeedback] playing available plan");
          robot_interface_->getPlanner()->playAnimation();
        }
      }

      if (group_menu_handles_.find(loop_key) != std::end(group_menu_handles_))
      {
        if (group_menu_handles_[loop_key] == feedback->menu_entry_id)
        {
          ROS_INFO("[AffordanceTemplate::processFeedback] changing looping functionality");

          MenuHandleKey key;
          key[feedback->marker_name] = {"Loop Animation"};

          bool loop = false;
          if(marker_menus_[feedback->marker_name].getCheckState( feedback->menu_entry_id, state ) ) 
          {
            if(state == interactive_markers::MenuHandler::CHECKED) 
            {
              marker_menus_[feedback->marker_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::UNCHECKED );
            }
            else
            {
              marker_menus_[feedback->marker_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
              loop = true; // transitioning from not looping to looping
            }
              
            marker_menus_[feedback->marker_name].apply( *server_, feedback->marker_name );
            robot_interface_->getPlanner()->loopAnimation(loop);
          }
          else
          {
            ROS_ERROR("can't get the loop state!!");
          }
        }
      }

      if (group_menu_handles_.find(autoplay_key) != std::end(group_menu_handles_))
      {
        if (group_menu_handles_[autoplay_key] == feedback->menu_entry_id)
        {
          ROS_WARN("[AffordanceTemplate::processFeedback] flipping autoplay functionality");

          MenuHandleKey key;
          key[feedback->marker_name] = {"Autoplay"};

          if(marker_menus_[feedback->marker_name].getCheckState( feedback->menu_entry_id, state ) ) 
          {
            if(state == interactive_markers::MenuHandler::CHECKED) 
            {
              marker_menus_[feedback->marker_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::UNCHECKED );
              autoplay_display_ = false;
            }
            else
            {
              marker_menus_[feedback->marker_name].setCheckState( group_menu_handles_[key], interactive_markers::MenuHandler::CHECKED );
              autoplay_display_ = true;
            }
              
            marker_menus_[feedback->marker_name].apply( *server_, feedback->marker_name );
          }
          else
          {
            ROS_ERROR("can't get the autoplay state!!");
          }
        }
      }

      break;
    }
    default : 
      ROS_WARN("[AffordanceTemplate::processFeedback] got unrecognized or unmatched menu event: %d", feedback->event_type);
      break;
  }
  server_->applyChanges();
  marker_menus_[feedback->marker_name].apply( *server_, feedback->marker_name );
}

bool AffordanceTemplate::isObject(const std::string& obj) {
  for(auto &o : structure_.display_objects) {
    if(o.name == obj) {
      return true;
    }
  }
  return false;
}

bool AffordanceTemplate::isWaypoint(const std::string& wp) {
  return !isObject(wp);
}


bool AffordanceTemplate::hasObjectFrame(std::string obj) {
  return isObject(obj) && (frame_store_.find(obj) != std::end(frame_store_)); 
}

bool AffordanceTemplate::hasWaypointFrame(std::string wp) {
  return isWaypoint(wp) && (frame_store_.find(wp) != std::end(frame_store_)); 
}

int AffordanceTemplate::getNumWaypoints(const AffordanceTemplateStructure structure, const std::string traj_name, const int ee_id) {

  for(auto &traj : structure.ee_trajectories) {
    if(traj.name == traj_name) {
      for(auto &ee_list : traj.ee_waypoint_list) {
        if(ee_list.id == ee_id) {
          return ee_list.waypoints.size();
        }
      }
    }
  }
  return 0;
}


bool AffordanceTemplate::computePathSequence(const AffordanceTemplateStructure structure, std::string traj_name, int ee_id, int idx, int steps, bool direct, bool backwards, std::vector<int> &sequence_ids, int &next_path_idx)
{ 
  sequence_ids.clear();
  if (direct)
  {
    sequence_ids.push_back(steps);
    next_path_idx = steps;
    return true;
  }
  else if (steps == 0) 
  {
    sequence_ids.push_back(idx);
    next_path_idx = idx;
    return true;
  } 
  else 
  {
    int max_idx = getNumWaypoints(structure, traj_name, ee_id)-1;
    int cap = max_idx+1;
    int inc = 1;
    if(backwards) {
      inc = -1;
    }
    if(idx == -1) {
      if(backwards) {
        sequence_ids.push_back(max_idx);
        idx = max_idx;
      } else {
        sequence_ids.push_back(0);
        idx = 0;
      }
      steps--;
    }
    for(int s=0; s<steps; s++) {
      idx += inc;
      if(idx == -1) {
        if(backwards) {
          idx = max_idx;
        } else {
          idx = 0;
        }
      } else {
        idx = idx%cap;
      }
      sequence_ids.push_back(idx);      
    }
    next_path_idx = sequence_ids.back();
  }
  return true;
}

void AffordanceTemplate::planRequest(const PlanGoalConstPtr& goal)
{
  ROS_INFO("[AffordanceTemplate::planRequest] request to plan for \"%s\" trajectory...", goal->trajectory.c_str());

  PlanResult result;
  PlanFeedback planning;

  planning.progress = 1;
  planning_server_.publishFeedback(planning);

  // @seth added 12/29
  // reset prediced display queue
  robot_interface_->getPlanner()->resetAnimation();

  for (auto ee : goal->groups) 
  {
    ++planning.progress;
    planning_server_.publishFeedback(planning);

    plan_status_[goal->trajectory][ee].plan_valid = false;
    plan_status_[goal->trajectory][ee].exec_valid = false;
    plan_status_[goal->trajectory][ee].direct     = goal->direct;
    plan_status_[goal->trajectory][ee].backwards  = goal->backwards;
    plan_status_[goal->trajectory][ee].sequence_ids.clear();
    plan_status_[goal->trajectory][ee].sequence_poses.clear();

    bool skip = true;
    
    // get our waypoints for this trajectory so we can get the EE pose IDs
    int ee_id = robot_interface_->getEEID(ee);
    std::vector<affordance_template_object::EndEffectorWaypoint> wp_vec;
    for(auto &traj : structure_.ee_trajectories) 
    {
      if(traj.name == goal->trajectory)
      {
        for(auto &ee_list : traj.ee_waypoint_list) 
        {
          if(ee_list.id == ee_id) 
          {
            wp_vec = ee_list.waypoints;
            skip = false;
          }
        }
      }
    }

    //
    // make sure the ee_id (thus, the EE) is in the WP list, if not move onto next EE
    if (skip)
      continue;

    int max_idx = getNumWaypoints(structure_, goal->trajectory, ee_id);
    int current_idx = plan_status_[goal->trajectory][ee].current_idx;
    std::string manipulator_name = robot_interface_->getManipulator(ee);
    std::map<std::string, std::vector<geometry_msgs::PoseStamped> > goals;

    sensor_msgs::JointState set_state;
    ContinuousPlan p;
    if (getContinuousPlan( goal->trajectory, (current_idx-1), manipulator_name, PlanningGroup::MANIPULATOR, p))
    {
      // index already has been planned for, get the start state
      set_state.header = p.plan.trajectory_.joint_trajectory.header;
      set_state.name = p.plan.trajectory_.joint_trajectory.joint_names;
      set_state.position = p.plan.trajectory_.joint_trajectory.points.back().positions;
      set_state.velocity = p.plan.trajectory_.joint_trajectory.points.back().velocities;
      set_state.effort = p.plan.trajectory_.joint_trajectory.points.back().effort;
      if (!robot_interface_->getPlanner()->setStartState(manipulator_name, set_state))
      {
        ROS_ERROR("[AffordanceTemplate::planRequest] failed to set initial state for %s", manipulator_name.c_str());
        planning.progress = -1;
        planning_server_.publishFeedback(planning);
        result.succeeded = false;
        planning_server_.setSucceeded(result);
        return;
      }
    }
    else
    {
      //
      // set the first start state to current state because we don't have a previous plan
      if (!robot_interface_->getPlanner()->getCurrentState(manipulator_name, set_state))
      {
        ROS_ERROR("[AffordanceTemplate::planRequest] failed to get initial state for %s", manipulator_name.c_str());
        planning.progress = -1;
        planning_server_.publishFeedback(planning);
        result.succeeded = false;
        planning_server_.setSucceeded(result);
        return; 
      }
      if (!robot_interface_->getPlanner()->setStartState(manipulator_name))
      {
        ROS_ERROR("[AffordanceTemplate::planRequest] failed to set initial state for %s", manipulator_name.c_str());
        planning.progress = -1;
        planning_server_.publishFeedback(planning);
        result.succeeded = false;
        planning_server_.setSucceeded(result);
        return;
      }
    }

    // make sure it matches our max idx number because that is max num waypoints
    if (wp_vec.size() != max_idx)
    {
      ROS_ERROR("[AffordanceTemplate::planRequest] waypoint vector size does not match up!!");
      planning.progress = -1;
      planning_server_.publishFeedback(planning);
      result.succeeded = false;
      planning_server_.setSucceeded(result);
      return;
    }

    // get list of EE pose names related to ID
    std::map<int, std::string> ee_pose_map = robot_interface_->getEEPoseNameMap(ee);

    // testing debug
    if (!computePathSequence(structure_, goal->trajectory, ee_id, 
                            plan_status_[goal->trajectory][ee].current_idx,
                            goal->steps, false,
                            plan_status_[goal->trajectory][ee].backwards, 
                            plan_status_[goal->trajectory][ee].sequence_ids, 
                            plan_status_[goal->trajectory][ee].goal_idx)
    {
      ROS_ERROR("[AffordanceTemplate::planRequest] failed to get path sequence!!");
      planning.progress = -1;
      planning_server_.publishFeedback(planning);
      result.succeeded = false;
      planning_server_.setSucceeded(result);
      return;
    }

    // now loop through waypoints setting new start state to the last planned joint values
    // FIXME - don't like how I'm doing this, just hacking to get it working for now..
    int idx = current_idx == -1 ? 0 : current_idx; // FIXME
    int num_steps;
    switch(goal->steps) // really dislike this
    {
      case -1:
        if (idx > 0)
          idx += goal->steps;
        num_steps = idx + 1; // current, "refresh" button
        break;
      case 0:
        num_steps = max_idx;
        break;
      default:
        num_steps = idx + goal->steps;
        break;
    } 
    if(goal->direct)  // FIXME
    {
      idx = goal->steps == max_idx ? max_idx-1 : 0;
      num_steps = idx + 1;
    } 
    for (; idx < num_steps && idx < max_idx; ++idx)
    {
      ++planning.progress;
      planning_server_.publishFeedback(planning);

      std::string next_path_str = createWaypointID(ee_id, idx);

      ROS_INFO("[AffordanceTemplate::planRequest] configuring plan goal for waypoint %s [%d/%d] for %s[%d] on manipulator=%s", next_path_str.c_str(), idx+1, max_idx, ee.c_str(), ee_id, manipulator_name.c_str());

      if (plan_status_[goal->trajectory][ee].direct)
      {
        plan_status_[goal->trajectory][ee].sequence_ids.clear();
        plan_status_[goal->trajectory][ee].sequence_ids.push_back(goal->steps);
        plan_status_[goal->trajectory][ee].goal_idx = goal->steps;
      }
      else if (!computePathSequence(structure_, goal->trajectory, ee_id, idx, 1, false,
                                    plan_status_[goal->trajectory][ee].backwards, 
                                    plan_status_[goal->trajectory][ee].sequence_ids, 
                                    plan_status_[goal->trajectory][ee].goal_idx)) // FIXME - may need current_idx instead of idx
      {
        ROS_ERROR("[AffordanceTemplate::planRequest] failed to get path sequence!!");
        planning.progress = -1;
        planning_server_.publishFeedback(planning);
        result.succeeded = false;
        planning_server_.setSucceeded(result);
        return;
      }

      for(auto id : plan_status_[goal->trajectory][ee].sequence_ids)
        ROS_WARN("sequence_ids in the loop %d", id);
      continue;

      // create goal
      goals[manipulator_name].clear();
      geometry_msgs::PoseStamped pt = frame_store_[next_path_str + "/tf"].second;    
      goals[manipulator_name].push_back(pt);
      plan_status_[goal->trajectory][ee].sequence_poses.push_back(pt);
    
      if (robot_interface_->getPlanner()->planPaths(goals, false, autoplay_display_)) 
      {
        ROS_INFO("[AffordanceTemplate::planRequest] planning for %s succeeded", next_path_str.c_str());
        ++planning.progress;
        planning_server_.publishFeedback(planning);

        // //########################  TAKE OUT  ##############################
        // //##################################################################
        // ros::Time start_t = ros::Time::now();
        // while(ros::ok() && ros::Time::now() - start_t < ros::Duration(2.0))
        // {
        //   ros::spinOnce();
        //   ros::Duration(0.01).sleep(); 
        // }
        // //##################################################################

        plan_status_[goal->trajectory][ee].plan_valid = true;
        plan_status_[goal->trajectory][ee].current_idx = plan_status_[goal->trajectory][ee].goal_idx;
        
        moveit::planning_interface::MoveGroup::Plan plan;
        if (!robot_interface_->getPlanner()->getPlan(manipulator_name, plan))
        {
          ROS_FATAL("[AffordanceTemplate::planRequest] couldn't find stored plan for %s waypoint!! this shouldn't happen, something is wrong!.", next_path_str.c_str());
          planning.progress = -1;
          planning_server_.publishFeedback(planning);
          result.succeeded = false;
          planning_server_.setSucceeded(result);
          return;
        }

        ContinuousPlan cp;
        cp.step = idx;
        cp.group = manipulator_name;
        cp.type = PlanningGroup::MANIPULATOR;
        cp.start_state = set_state;
        cp.plan = plan;
        setContinuousPlan(goal->trajectory, cp);

        //
        // set the start state for ee planning
        set_state.header = plan.trajectory_.joint_trajectory.header;
        set_state.name = plan.trajectory_.joint_trajectory.joint_names;
        set_state.position = plan.trajectory_.joint_trajectory.points.back().positions;
        set_state.velocity = plan.trajectory_.joint_trajectory.points.back().velocities;
        set_state.effort = plan.trajectory_.joint_trajectory.points.back().effort;
        if (!robot_interface_->getPlanner()->setStartState(manipulator_name, set_state))
        {
          ROS_ERROR("[AffordanceTemplate::planRequest] failed to set start state for %s", manipulator_name.c_str());
          planning.progress = -1;
          planning_server_.publishFeedback(planning);
          result.succeeded = false;
          planning_server_.setSucceeded(result);
          return;
        }

        // find and add EE joint state to goal
        if (ee_pose_map.find(wp_vec[current_idx].ee_pose) == ee_pose_map.end())
        {
          ROS_WARN("[AffordanceTemplate::planRequest] couldn't find EE Pose ID %d in robot interface map!!", current_idx);
        }
        else
        {
          ++planning.progress;
          planning_server_.publishFeedback(planning);

          ROS_INFO("[AffordanceTemplate::planRequest] setting EE goal pose to %s", ee_pose_map[wp_vec[current_idx].ee_pose].c_str());
          sensor_msgs::JointState ee_js;
          try 
          {
            if (!robot_interface_->getPlanner()->getRDFModel()->getGroupState( ee, ee_pose_map[wp_vec[current_idx].ee_pose], ee_js)) // this is the reason for the try{} block, TODO should put null pointer detection in
            {
              ROS_ERROR("[AffordanceTemplate::planRequest] couldn't get group state!!");
              planning.progress = -1;
              planning_server_.publishFeedback(planning);
              result.succeeded = false;
              planning_server_.setSucceeded(result);
              return;
            }
            else
            {
              ++planning.progress;
              planning_server_.publishFeedback(planning);

              std::map<std::string, std::vector<sensor_msgs::JointState> > ee_goals;
              ee_goals[ee].push_back(ee_js);
              if (!robot_interface_->getPlanner()->planJointPath( ee_goals, false, autoplay_display_))
              {
                ROS_ERROR("[AffordanceTemplate::planRequest] couldn't plan for gripper joint states!!");
                planning.progress = -1;
                planning_server_.publishFeedback(planning);
                result.succeeded = false;
                planning_server_.setSucceeded(result);
                return;
              }

              if (!robot_interface_->getPlanner()->getPlan(ee, plan))
              {
                ROS_FATAL("[AffordanceTemplate::planRequest] couldn't find stored plan for %s waypoint!! this shouldn't happen, something is wrong!.", next_path_str.c_str());
                planning.progress = -1;
                planning_server_.publishFeedback(planning);
                result.succeeded = false;
                planning_server_.setSucceeded(result);
                return;
              }

              cp.step = idx;
              cp.group = ee;
              cp.type = PlanningGroup::EE;
              cp.start_state = set_state;
              cp.plan = plan;
              setContinuousPlan(goal->trajectory, cp);

              //
              // doing this lets us append the grasp pose without having the arm go back to init start state
              ContinuousPlan p;
              getContinuousPlan( goal->trajectory, idx, manipulator_name, PlanningGroup::MANIPULATOR, p);
              p.plan.trajectory_.joint_trajectory.points.push_back(plan.trajectory_.joint_trajectory.points.back());

              // set the start state for next iteration
              set_state.header = plan.trajectory_.joint_trajectory.header;
              set_state.name = plan.trajectory_.joint_trajectory.joint_names;
              set_state.position = plan.trajectory_.joint_trajectory.points.back().positions;
              set_state.velocity = plan.trajectory_.joint_trajectory.points.back().velocities;
              set_state.effort = plan.trajectory_.joint_trajectory.points.back().effort;
              if (!robot_interface_->getPlanner()->setStartState(ee, set_state))
              {
                ROS_ERROR("[AffordanceTemplate::planRequest] failed to set start state for %s", ee.c_str());
                planning.progress = -1;
                planning_server_.publishFeedback(planning);
                result.succeeded = false;
                planning_server_.setSucceeded(result);
                return;
              }
            }
          } 
          catch(...)
          {
            ROS_FATAL("[AffordanceTemplate::planRequest] couldn't get planner or RDF model -- bad pointer somewhere!!");
            planning.progress = -1;
            planning_server_.publishFeedback(planning);
            result.succeeded = false;
            planning_server_.setSucceeded(result);
            return;
          }
        }

        // keep track of the current index based on what we planned
        // current_idx = plan_status_[goal->trajectory][ee].current_idx;

        ++planning.progress;
        planning_server_.publishFeedback(planning);
      } 
      else
      {
        ROS_ERROR("[AffordanceTemplate::planRequest] planning failed for waypoint %s", next_path_str.c_str());
        planning.progress = -1;
        planning_server_.publishFeedback(planning);
        result.succeeded = false;
        planning_server_.setSucceeded(result);
        return;
      }
    } // waypoint loop
  } // ee loop

  // ++planning.progress;
  // planning_server_.publishFeedback(planning);

  // if (goal->execute_on_plan)
  // {
  //   ++planning.progress;
  //   planning_server_.publishFeedback(planning);

  //   for (auto ee : goal->groups)
  //   {
  //     if ( plan_status_[goal->trajectory][ee].plan_valid)
  //     {
  //       if (!continuousMoveToWaypoints(goal->trajectory, ee, plan_status_[goal->trajectory][ee].current_idx, goal->steps))
  //       {
  //         ROS_ERROR("[AffordanceTemplate::planRequest] execution of plan failed!!");
  //         planning.progress = -1;
  //         planning_server_.publishFeedback(planning);
  //         result.succeeded = false;
  //         planning_server_.setSucceeded(result);
  //         return;
  //       }
  //     }
  //     else
  //     {
  //       ROS_WARN("[AffordanceTemplate::planRequest] no valid plan found for EE %s to execute", ee.c_str());
  //     }
  //   }
  // }

  ++planning.progress;
  planning_server_.publishFeedback(planning);
  
  result.succeeded = true;
  planning_server_.setSucceeded(result);
}


void AffordanceTemplate::executeRequest(const ExecuteGoalConstPtr& goal)
{
  ROS_INFO("[AffordanceTemplate::executeRequest] request to execute any stored plans...");

  ExecuteResult result;
  ExecuteFeedback exe;

  exe.progress = 1;
  execution_server_.publishFeedback(exe);
  
  for (auto ee : goal->groups)
  {
    if ( plan_status_[goal->trajectory][ee].plan_valid)
    {
      if (!continuousMoveToWaypoints(goal->trajectory, ee))
      {
        ROS_ERROR("[AffordanceTemplate::executeRequest] execution of plan failed!!");
        exe.progress = -1;
        execution_server_.publishFeedback(exe);
        result.succeeded = false;
        execution_server_.setSucceeded(result);
        
        // clear out whatever plans we may have
        continuous_plans_[goal->trajectory].clear();

        // @seth TODO 12/30 should increment the current index for the trajectory plan
        // the plan button is for "from the waypoint i am currently add, i should plan num_steps ahead"
        // you only increment the counter after you execute
        // so the idea is you start at -1, cause you are not "on" the waypoint trajectory
        // you can plan to the 1st waypoint, or n-steps through from -1
    
        return;
      }
    }
  }

  continuous_plans_[goal->trajectory].clear();
  
  result.succeeded = true;
  execution_server_.setSucceeded(result);
}


// list of ee names, steps, direct, backwards; return map of bools keyed on EE name
std::map<std::string, bool> AffordanceTemplate::planPathToWaypoints(const std::vector<std::string>& ee_names, int steps, bool direct, bool backwards, bool use_current)
{
  ROS_INFO("AffordanceTemplate::planPathToWaypoints() planning for %d ee's", (int)ee_names.size());

  std::map<std::string, bool> ret;
  std::map<std::string, std::vector<geometry_msgs::PoseStamped> > goals;

  for(auto ee: ee_names) {
    ret[ee] = false;
 
    int ee_id = robot_interface_->getEEID(ee);
    int max_idx = getNumWaypoints(structure_, current_trajectory_, ee_id);
    std::string manipulator_name = robot_interface_->getManipulator(ee);

    plan_status_[current_trajectory_][ee].backwards = backwards;
    plan_status_[current_trajectory_][ee].direct = direct;
    plan_status_[current_trajectory_][ee].plan_valid = false;
    plan_status_[current_trajectory_][ee].exec_valid = false;
    plan_status_[current_trajectory_][ee].sequence_ids.clear();
    plan_status_[current_trajectory_][ee].sequence_poses.clear();

    int current_idx = plan_status_[current_trajectory_][ee].current_idx;

    ROS_INFO("AffordanceTemplate::planPathToWaypoints() -- configuring plan goal for %s[%d]. manipulator=%s, size=%d", ee.c_str(), ee_id,manipulator_name.c_str(), max_idx);

    std::vector<int> sequence_ids;
    int next_path_idx;
    if(computePathSequence(structure_, current_trajectory_, ee_id, current_idx, steps, direct, backwards, sequence_ids, next_path_idx)) {    
      ROS_INFO("AffordanceTemplate::planPathToWaypoints() -- got path sequence");
      // std::cout << "path sequence: [ ";
      // for (auto i: sequence_ids) {
      //   std::cout << i << " ";
      // }
      // std::cout << "]" << std::endl;      
    } else {
      ROS_ERROR("AffordanceTemplate::planPathToWaypoints() -- failed to get path sequence!!");
      return ret;
    }
    if(direct) {
      sequence_ids.clear();
      sequence_ids.push_back(next_path_idx);
      ROS_INFO("AffordanceTemplate::planPathToWaypoints() -- moving direct");
    }

    if (use_current)
    {
      ROS_WARN("[AffordanceTemplate::planPathToWaypoints] setting robot start state to current joint state");
      if (!robot_interface_->getPlanner()->setStartState(manipulator_name))
      {
        ROS_ERROR("[AffordanceTemplate::planPathToWaypoints] failed to set start state for %s", ee.c_str());
      }
    }
    else
    {
      ROS_WARN("[AffordanceTemplate::planPathToWaypoints] WOULD SET START STATE TO SOME OTHER JOINT STATE HEREEEEE");
    }

    plan_status_[current_trajectory_][ee].sequence_ids = sequence_ids;
    plan_status_[current_trajectory_][ee].goal_idx = next_path_idx;

    std::string next_path_str = createWaypointID(ee_id, next_path_idx);
    if(direct) {
      ROS_INFO("AffordanceTemplate::planPathToWaypoints() -- computing DIRECT path to waypoint[%d]: %s", next_path_idx, next_path_str.c_str());
    } else {
      ROS_INFO("AffordanceTemplate::planPathToWaypoints() -- computing path to waypoint[%d]: %s", next_path_idx, next_path_str.c_str());
    }

    goals[manipulator_name].clear();
    for(auto &idx : plan_status_[current_trajectory_][ee].sequence_ids) {
      next_path_str = createWaypointID(ee_id, idx);
      ROS_INFO("AffordanceTemplate::planPathToWaypoints() -- next goal: %s", next_path_str.c_str());
      geometry_msgs::PoseStamped pt = frame_store_[next_path_str + "/tf"].second;    
      goals[manipulator_name].push_back(pt);
      plan_status_[current_trajectory_][ee].sequence_poses.push_back(pt);
    }
  
  }

  bool result = robot_interface_->getPlanner()->planPaths(goals, false, true);
  ROS_INFO("AffordanceTemplate::planPathToWaypoints() -- planner returned with %d", (int)result);

  if(result) 
  {
    ROS_INFO("AffordanceTemplate::planPathToWaypoints() -- planning succeeded");

    for(auto ee: ee_names) 
    {
      plan_status_[current_trajectory_][ee].plan_valid = true;
      ret[ee] = true;
    }
  } 
  else
    ROS_WARN("AffordanceTemplate::planPathToWaypoints() -- planning failed");

  return ret;
}


bool AffordanceTemplate::continuousMoveToWaypoints(const std::string& trajectory, const std::string& ee)
{
  if (continuous_plans_.find(trajectory) == continuous_plans_.end())
  {
    ROS_ERROR("[AffordanceTemplate::continuousMoveToWaypoints] no plan found for trajectory %s!!", trajectory.c_str());
    return false;
  }

  if (!plan_status_[trajectory][ee].plan_valid) 
  {
    plan_status_[trajectory][ee].exec_valid = false;
    ROS_ERROR("[AffordanceTemplate::continuousMoveToWaypoints] EE %s in trajectory %s doesn't have a valid plan!!", ee.c_str(), trajectory.c_str());
    return false;
  }

  std::string manipulator_name = robot_interface_->getManipulator(ee);
  std::vector<std::pair<std::string, moveit::planning_interface::MoveGroup::Plan> > plans_to_exe;
  for ( auto& p : continuous_plans_[trajectory])
    if (p.group == ee || p.group == manipulator_name)
      plans_to_exe.push_back(std::make_pair(p.group, p.plan));

  if (!robot_interface_->getPlanner()->executeContinuousPlans(plans_to_exe))
  {
    ROS_ERROR("[AffordanceTemplate::continuousMoveToWaypoints] execution failed");
    return false;
  }
  ROS_INFO("[AffordanceTemplate::moveToWaypoints] execution succeeded!!");

  plan_status_[trajectory][ee].current_idx = plan_status_[trajectory][ee].goal_idx;
  plan_status_[trajectory][ee].plan_valid = false;
  plan_status_[trajectory][ee].exec_valid = true;

  return true;
}


// list of ee waypoints to move to, return true if all waypoints were valid
bool AffordanceTemplate::moveToWaypoints(const std::vector<std::string>& ee_names) 
{
  // ROS_INFO("AffordanceTemplate::moveToWaypoints() with size %d", ee_names.size());
  std::vector<std::string> valid_ee_plans;
  std::vector<std::string> m_names;
  for(auto ee: ee_names) {
    if (plan_status_[current_trajectory_][ee].plan_valid) {
      valid_ee_plans.push_back(ee);
      m_names.push_back(robot_interface_->getManipulator(ee));
    } else {
      plan_status_[current_trajectory_][ee].exec_valid = false;
    }
  }
  if(robot_interface_->getPlanner()->executePlans(m_names)) {
    ROS_INFO("AffordanceTemplate::moveToWaypoints() -- execution succeeded");
    for(auto ee: valid_ee_plans) {
      plan_status_[current_trajectory_][ee].current_idx = plan_status_[current_trajectory_][ee].goal_idx;
      plan_status_[current_trajectory_][ee].plan_valid = false;
      plan_status_[current_trajectory_][ee].exec_valid = true;
    }
    return true;
  }
  ROS_WARN("AffordanceTemplate::moveToWaypoints() -- execution failed");
  return false;
}


void AffordanceTemplate::update() {
  ros::Rate loop_rate(loop_rate_);
  tf::Transform transform;
  FrameInfo fi;
  ros::Time t = ros::Time::now();
  
  ROS_INFO("AffordanceTemplate::udpate() -- updating...");
  if(running_)
  {
    for(auto &f: frame_store_) 
    {
      fi = f.second;
      tf::poseMsgToTF(fi.second.pose, transform);
      tf_broadcaster_.sendTransform(tf::StampedTransform(transform, t, fi.second.header.frame_id, fi.first));
      loop_rate.sleep();
    }
  }
}


void AffordanceTemplate::run()
{
  ros::Rate loop_rate(loop_rate_);
  tf::Transform transform;
  FrameInfo fi;
  ros::Time t;
  
  mutex_.lock();
  
  ROS_INFO("AffordanceTemplate::run() -- spinning...");
  while(running_ && ros::ok())
  {
    t = ros::Time::now();
    for(auto &f: frame_store_) 
    {
      fi = f.second;
      tf::poseMsgToTF(fi.second.pose, transform);
      tf_broadcaster_.sendTransform(tf::StampedTransform(transform, t, fi.second.header.frame_id, fi.first));
    }
    loop_rate.sleep();
  }
  mutex_.unlock();
  ROS_INFO("AffordanceTemplate::run() -- leaving spin thread. template must be shutting down...");
}


void AffordanceTemplate::stop()
{
  ROS_WARN("[AffordanceTemplate::stop] %s being asked to stop..", name_.c_str());
  running_ = false;
  removeAllMarkers();
}


bool AffordanceTemplate::setObjectScaling(const std::string& key, double scale_factor, double ee_scale_factor)
{ 
  ROS_DEBUG("[AffordanceTemplate::setObjectScaling] setting object %s scaling to %g, %g", key.c_str(), scale_factor, ee_scale_factor);
  object_scale_factor_[key] = scale_factor;
  ee_scale_factor_[key] = ee_scale_factor;

  removeAllMarkers();
  return createFromStructure(structure_, true, false, current_trajectory_);
}


bool AffordanceTemplate::setObjectPose(const DisplayObjectInfo& obj)
{
  bool found = false;
  
  ROS_INFO("[AffordanceTemplate::setObjectPose] setting pose for object %s in template %s:%d", obj.name.c_str(), obj.type.c_str(), obj.id);
  for (auto& d : structure_.display_objects)
  {
    std::string obj_name = obj.name + ":" + std::to_string(obj.id);
    if (d.name == obj_name)
    {
      ROS_INFO("[AffordanceTemplate::setObjectPose] matched object %s in frame: %s", obj_name.c_str(), obj.stamped_pose.header.frame_id.c_str());

      geometry_msgs::PoseStamped ps;
      try {
        ros::Time now = ros::Time::now();
        tf_listener_.waitForTransform(frame_store_[obj_name].second.header.frame_id, obj.stamped_pose.header.frame_id, now, ros::Duration(3.0));
        tf_listener_.transformPose(frame_store_[obj_name].second.header.frame_id, obj.stamped_pose, ps);
      } catch(...) {
        ROS_WARN("tf lookup error");  
      }
      frame_store_[obj_name].second = ps;

      server_->setPose(obj_name, ps.pose);
              
      found = true;
      break;
    }
  }
  server_->applyChanges();
  return found;
}


bool AffordanceTemplate::getContinuousPlan(const std::string& trajectory, const int step, const std::string& group, const PlanningGroup type, ContinuousPlan& plan)
{
  if (continuous_plans_.find(trajectory) == continuous_plans_.end())
  {
    ROS_WARN("[AffordanceTemplate::getContinuousPlan] no plan found for trajectory %s", trajectory.c_str());
    return false;
  }

  if (step < 0)
    return false;

  for ( auto& p : continuous_plans_[trajectory])
  {
    if (p.step == step && p.group == group && p.type == type)
    {
      plan = p;
      return true;
    }
  }
  return false;
}


void AffordanceTemplate::setContinuousPlan(const std::string& trajectory, const ContinuousPlan& plan)
{
  ROS_WARN("trajectory %s plan step is %d for group %s", trajectory.c_str(), plan.step, plan.group.c_str());
  if (continuous_plans_.find(trajectory) == continuous_plans_.end())
  {
    continuous_plans_[trajectory].push_back(plan);
  }
  else
  {
    for (auto& cp : continuous_plans_[trajectory])
    {
      if (cp.step == plan.step && cp.group == plan.group && cp.type == plan.type)
      {
        cp.start_state = plan.start_state;
        cp.plan = plan.plan;
        return;
      }
    }

    // else not in there yet
    continuous_plans_[trajectory].push_back(plan);
  }

  ROS_DEBUG("[AffordanceTemplate::setContinuousPlan] there are now %d plans", continuous_plans_[trajectory].size());
}