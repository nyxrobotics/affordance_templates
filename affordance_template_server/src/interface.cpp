#include <affordance_template_server/interface.h>

using namespace affordance_template_server;
using namespace affordance_template_msgs;
using namespace affordance_template_object;

AffordanceTemplateInterface::AffordanceTemplateInterface(const std::string &_robot_name)
{
    ROS_INFO("[AffordanceTemplateInterface] starting...");

    ros::NodeHandle nh;

    if (!_robot_name.empty())
        ROS_INFO("[AffordanceTemplateInterface] creating server using robot %s", _robot_name.c_str());
    at_server_ = new AffordanceTemplateServer(_robot_name);

    const std::string base_srv = "/affordance_template_server/";
    at_srv_map_["get_robots"]              = nh.advertiseService(base_srv + "get_robots", &AffordanceTemplateInterface::handleRobotRequest, this);
    at_srv_map_["get_templates"]           = nh.advertiseService(base_srv + "get_templates", &AffordanceTemplateInterface::handleTemplateRequest, this);
    at_srv_map_["load_robot"]              = nh.advertiseService(base_srv + "load_robot", &AffordanceTemplateInterface::handleLoadRobot, this);
    at_srv_map_["add_template"]            = nh.advertiseService(base_srv + "add_template", &AffordanceTemplateInterface::handleAddTemplate, this);
    at_srv_map_["delete_template"]         = nh.advertiseService(base_srv + "delete_template", &AffordanceTemplateInterface::handleDeleteTemplate, this);
    at_srv_map_["get_running"]             = nh.advertiseService(base_srv + "get_running", &AffordanceTemplateInterface::handleRunning, this);
    at_srv_map_["plan_command"]            = nh.advertiseService(base_srv + "plan_command", &AffordanceTemplateInterface::handlePlanCommand, this);
    at_srv_map_["execute_command"]         = nh.advertiseService(base_srv + "execute_command", &AffordanceTemplateInterface::handleExecuteCommand, this);
    at_srv_map_["save_template"]           = nh.advertiseService(base_srv + "save_template", &AffordanceTemplateInterface::handleSaveTemplate, this);
    at_srv_map_["add_trajectory"]          = nh.advertiseService(base_srv + "add_trajectory", &AffordanceTemplateInterface::handleAddTrajectory, this);
    at_srv_map_["scale_object"]            = nh.advertiseService(base_srv + "scale_object", &AffordanceTemplateInterface::handleObjectScale, this);
    at_srv_map_["get_template_status"]     = nh.advertiseService(base_srv + "get_template_status", &AffordanceTemplateInterface::handleTemplateStatus, this);
    at_srv_map_["get_status"]              = nh.advertiseService(base_srv + "get_status", &AffordanceTemplateInterface::handleServerStatus, this);
    at_srv_map_["set_template_trajectory"] = nh.advertiseService(base_srv + "set_template_trajectory", &AffordanceTemplateInterface::handleSetTrajectory, this);
    at_srv_map_["set_template_pose"]       = nh.advertiseService(base_srv + "set_template_pose", &AffordanceTemplateInterface::handleSetPose, this);
    ROS_INFO("[AffordanceTemplateInterface] services set up...");

    ROS_INFO("[AffordanceTemplateInterface] robot ready!!");
}

AffordanceTemplateInterface::~AffordanceTemplateInterface() 
{
    delete at_server_;
}

bool AffordanceTemplateInterface::handleRobotRequest(GetRobotConfigInfo::Request &req, GetRobotConfigInfo::Response &res)
{
    at_server_->setStatus(false);
    
    std::vector<RobotConfig> configs;
    if (!req.name.empty() && at_server_->findConfig(req.name))
    {
        ROS_INFO("[AffordanceTemplateInterface::handleRobotRequest] requesting robot info for %s", req.name.c_str());
        configs = at_server_->getRobotConfig(req.name);
        if (configs.size())
            res.robots.push_back(configs.front());
        else
            ROS_WARN("[AffordanceTemplateInterface::handleRobotRequest] couldn't find config matching robot name: %s on server!!", req.name.c_str());
    }
    else
    {
        ROS_INFO("[AffordanceTemplateInterface::handleRobotRequest] requesting robot info for all available robots");
        configs = at_server_->getRobotConfig();
        if (configs.size())
            res.robots = configs;
        else
            ROS_WARN("[AffordanceTemplateInterface::handleRobotRequest] couldn't find any robot configs on server!!");
    }

    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleTemplateRequest(GetAffordanceTemplateConfigInfo::Request &req, GetAffordanceTemplateConfigInfo::Response &res)
{
    at_server_->setStatus(false);
    
    std::vector<AffordanceTemplateConfig> templates;
    if (!req.name.empty() && at_server_->findTemplate(req.name))
    {
        ROS_INFO("[AffordanceTemplateInterface::handleTemplateRequest] requesting %s template info", req.name.c_str());
        templates = at_server_->getTemplate(req.name);
        if (templates.size())
            res.templates.push_back(templates.front());
        else
            ROS_WARN("[AffordanceTemplateInterface::handleTemplateRequest] couldn't find template on server matching template name: %s on server!!", req.name.c_str());
    }
    else
    {
        ROS_INFO("[AffordanceTemplateInterface::handleTemplateRequest] requesting infor for all loaded templates");
        templates = at_server_->getTemplate();
        if (templates.size())
            res.templates = templates;
        else
            ROS_WARN("[AffordanceTemplateInterface::handleTemplateRequest] couldn't find any templates on server!!");
    }
    
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleLoadRobot(LoadRobotConfig::Request &req, LoadRobotConfig::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleLoadRobot] loading robot %s", req.robot_config.name.c_str());

    res.status = false;

    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleAddTemplate(AddAffordanceTemplate::Request &req, AddAffordanceTemplate::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleAddTemplate]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleDeleteTemplate(DeleteAffordanceTemplate::Request &req, DeleteAffordanceTemplate::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleDeleteTemplate]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleRunning(GetRunningAffordanceTemplates::Request &req, GetRunningAffordanceTemplates::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleRunning]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handlePlanCommand(AffordanceTemplatePlanCommand::Request &req, AffordanceTemplatePlanCommand::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handlePlanCommand]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleExecuteCommand(AffordanceTemplateExecuteCommand::Request &req, AffordanceTemplateExecuteCommand::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleExecuteCommand]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleSaveTemplate(SaveAffordanceTemplate::Request &req, SaveAffordanceTemplate::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleSaveTemplate]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleAddTrajectory(AddAffordanceTemplateTrajectory::Request &req, AddAffordanceTemplateTrajectory::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleAddTrajectory]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleObjectScale(ScaleDisplayObject::Request &req, ScaleDisplayObject::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleObjectScale]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleTemplateStatus(GetAffordanceTemplateStatus::Request &req, GetAffordanceTemplateStatus::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleTemplateStatus]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleServerStatus(GetAffordanceTemplateServerStatus::Request &req, GetAffordanceTemplateServerStatus::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleServerStatus]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleSetTrajectory(SetAffordanceTemplateTrajectory::Request &req, SetAffordanceTemplateTrajectory::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleSetTrajectory]");
    at_server_->setStatus(true);
    return true;
}

bool AffordanceTemplateInterface::handleSetPose(SetAffordanceTemplatePose::Request &req, SetAffordanceTemplatePose::Response &res)
{
    at_server_->setStatus(false);
    ROS_INFO("[AffordanceTemplateInterface::handleSetPose]");
    at_server_->setStatus(true);
    return true;
}


void AffordanceTemplateInterface::handleObjectScaleCallback(const ScaleDisplayObjectInfo &data)
{
    ROS_INFO_STREAM("[AffordanceTemplateInterface::handleObjectScaleCallback] scale "<<data.class_type<<":"<<data.id<<"->object["<<data.object_name<<"] by ("<<data.scale_factor<<", "<<data.end_effector_scale_factor<<")");
    try
    {
        // TODO
        // key = str(data.class_type) + ":" + str(data.id)
        // self.server.at_data.class_map[data.class_type][data.id].scale_object(data.object_name, data.scale_factor, data.end_effector_scale_factor)
    }
    catch(...)
    {
        ROS_ERROR("[AffordanceTemplateInterface::handleObjectScaleCallback] error trying to scale object!!");
    }
}

bool AffordanceTemplateInterface::getTemplateStatus(AffordanceTemplateStatus &status, std::string template_name, int template_id, std::string traj_name, std::string frame_id="")
{
    return true;
}
