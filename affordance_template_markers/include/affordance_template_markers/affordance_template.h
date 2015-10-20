#ifndef _AFFORDANCE_TEMPLATE_H_
#define _AFFORDANCE_TEMPLATE_H_

#include <ros/ros.h>
#include <ros/package.h>

#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>

#include <interactive_markers/interactive_marker_server.h>
#include <interactive_markers/menu_handler.h>
#include <utils/marker_helper.h>

#include <sensor_msgs/JointState.h>
#include <geometry_msgs/Pose.h>

#include <affordance_template_markers/robot_interface.h>

#include <affordance_template_library/affordance_template_structure.h>
#include <affordance_template_library/affordance_template_parser.h>

// #include <affordance_template_msgs/RobotConfig.h>
// #include <affordance_template_msgs/EndEffectorConfig.h>
// #include <affordance_template_msgs/EndEffectorPoseData.h>

namespace affordance_template 
{
  class AffordanceTemplate
  {

  public:
    
    AffordanceTemplate(const ros::NodeHandle nh, 
                       boost::shared_ptr<interactive_markers::InteractiveMarkerServer> server, 
                       std::string robot_name, 
                       std::string template_type,
                       int id);
    ~AffordanceTemplate();

    void run();

    void setRobotInterface(boost::shared_ptr<affordance_template_markers::RobotInterface> robot_interface);
    void setupMenuOptions();

    bool loadFromFile(std::string filename, geometry_msgs::Pose pose, affordance_template_object::AffordanceTemplateStructure &structure);


  private:

    // menu config will hold the menu text as well as bool for whether it should have a checkbox
    typedef std::pair<std::string, bool> MenuConfig;

    // this will handle menus. first item is group name, vector list is nested menu text(s)
    typedef std::map<std::string, std::vector<std::string> > MenuHandleKey;

    // this is a storage DS to store "named" pose info. i.e., frame 'first' in pose.header.frame_id frame 
    typedef std::pair<std::string, geometry_msgs::PoseStamped> FrameInfo;

    ros::NodeHandle nh_;
    tf::TransformListener tf_listener_;
    tf::TransformBroadcaster tf_broadcaster_;

    // bookkeeping and IDs
    std::string robot_name_;
    std::string template_type_;
    std::string name_;
    std::string key_;
    std::string root_object_;
    int id_;
    double loop_rate_;
    bool object_controls_display_on_;

    // stored frames and poses
    tf::Transform robotTroot_;
    geometry_msgs::Pose markerPoseOffset_;

    boost::shared_ptr<interactive_markers::InteractiveMarkerServer> server_;
    boost::shared_ptr<affordance_template_markers::RobotInterface> robot_interface_;

    std::map<std::string, visualization_msgs::InteractiveMarker> int_markers_;
    std::map<std::string, interactive_markers::MenuHandler> marker_menus_;
    std::map<MenuHandleKey, interactive_markers::MenuHandler::EntryHandle> group_menu_handles_;
    std::map<std::string, AffordanceTemplate::FrameInfo> frame_store_;

    std::vector<MenuConfig> object_menu_options_;
    std::vector<MenuConfig> waypoint_menu_options_;
      
    affordance_template_object::AffordanceTemplateParser at_parser_;
    affordance_template_object::AffordanceTemplateStructure initial_structure_;
    // affordance_template_object::AffordanceTemplateStructure structure_;

    std::string current_trajectory_;

    std::map<std::string, double> object_scale_factor_;

    std::string getRootObject() { return root_object_; }
    void setRootObject(std::string root_object) { root_object_ = root_object; }

    std::string appendID(std::string s);
    bool appendIDToStructure(affordance_template_object::AffordanceTemplateStructure &structure);
    bool createFromStructure(affordance_template_object::AffordanceTemplateStructure structure, bool keep_poses=false, std::string traj="");

    void addInteractiveMarker(visualization_msgs::InteractiveMarker m);
    
    void setupObjectMenu(affordance_template_object::AffordanceTemplateStructure structure, affordance_template_object::DisplayObject obj);
    void setupWaypointMenu(affordance_template_object::AffordanceTemplateStructure structure, affordance_template_object::EndEffectorWaypointList ee);
    void setupSimpleMenuItem(affordance_template_object::AffordanceTemplateStructure structure, const std::string& name, const std::string& menu_text, bool has_check_box);
    void setupTrajectoryMenu(affordance_template_object::AffordanceTemplateStructure structure, const std::string& name);

    bool hasObjectFrame(std::string obj);

    geometry_msgs::Pose originToPose(affordance_template_object::Origin origin);


    void processFeedback(const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback);


  };
}

#endif