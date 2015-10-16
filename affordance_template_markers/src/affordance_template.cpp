#include <affordance_template_markers/affordance_template.h>

using namespace affordance_template;
using namespace affordance_template_object;
using namespace affordance_template_markers;

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
  loop_rate_(10.0)
{

  setupMenuOptions();

  ROS_INFO("AffordanceTemplate::init() -- Done Creating new Empty AffordanceTemplate");

}


AffordanceTemplate::~AffordanceTemplate() 
{
	
}

void AffordanceTemplate::spin()
{

  ros::AsyncSpinner spinner(1.0/loop_rate_);
  spinner.start();

  ROS_INFO("%s spinning.", nh_.getNamespace().c_str());
  ros::Rate loop_rate(loop_rate_);
  while(ros::ok())
  {
    loop_rate.sleep();
  }
  
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

}

bool AffordanceTemplate::loadFromFile(std::string filename, geometry_msgs::Pose pose, AffordanceTemplateStructure &structure)
{

  at_parser_.loadFromFile(filename, structure);
  appendIDToStructure(structure);
  // self.load_initial_parameters(pose)
  // self.create_from_parameters()
  // stuff = filename.split("/")
  // self.filename = stuff[len(stuff)-1]
  // return self.structure
  return true;
}

bool AffordanceTemplate::appendIDToStructure(AffordanceTemplateStructure &structure) {

  structure.name = appendID(structure.name);
  for(auto &obj : structure.display_objects) {
      obj.name = appendID(obj.name);
      if(obj.parent != "") {
        obj.parent = appendID(obj.parent);
      }
  }

  //       for obj in structure['display_objects'] :
  //           obj['name'] = self.append_id(obj['name'])
  //           try :
  //               obj['parent'] = self.append_id(obj['parent'])
  //           except :
  //               rospy.logwarn(str("AffordanceTemplate::append_id_to_structure() -- no parent for " + obj['name']))
  //       for traj in structure['end_effector_trajectory'] :
  //           for ee_group in traj['end_effector_group'] :
  //               for wp in ee_group['end_effector_waypoint'] :
  //                   wp['display_object'] = self.append_id(wp['display_object'])

  return true;

}
 
std::string AffordanceTemplate::appendID(std::string s) {
  return s + ":" + std::to_string(id_);
} 

int main(int argc, char **argv)
{
 
  ros::init(argc, argv, "affordance_template_test");
  ros::NodeHandle nh("~");
 
  std::string robot_name, template_type;
  
  if (nh.hasParam("robot_name")) {
    nh.getParam("robot_name", robot_name); 
  } else {
    robot_name = "r2_upperbody";
  }

  if (nh.hasParam("template_type")) {
    nh.getParam("template_type", template_type); 
  } else {
    template_type = "wheel";
  }

  boost::shared_ptr<affordance_template_markers::RobotInterface> robot_interface;
  robot_interface.reset(new affordance_template_markers::RobotInterface());
  robot_interface->load("r2_upperbody.yaml");

  boost::shared_ptr<interactive_markers::InteractiveMarkerServer> server;
  server.reset( new interactive_markers::InteractiveMarkerServer(std::string(robot_name + "_affordance_template_server"),"",false) );

  AffordanceTemplate at(nh, server, robot_name, template_type, 0);
  at.setRobotInterface(robot_interface);
  
  AffordanceTemplateStructure structure;
  geometry_msgs::Pose p;
  at.loadFromFile("/home/swhart/ros/catkin_workspace/src/affordance_templates/affordance_template_library/templates/wheel.json", p, structure);

  at.spin();
 
  return 0;
}


