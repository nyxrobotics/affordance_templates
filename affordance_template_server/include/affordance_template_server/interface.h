#ifndef _AFFORDANCE_TEMPLATE_INTERFACE_H_
#define _AFFORDANCE_TEMPLATE_INTERFACE_H_

#include <affordance_template_server/server.h>

#include <affordance_template_msgs/GetRobotConfigInfo.h>
#include <affordance_template_msgs/GetAffordanceTemplateConfigInfo.h>
#include <affordance_template_msgs/LoadRobotConfig.h>
#include <affordance_template_msgs/AddAffordanceTemplate.h>
#include <affordance_template_msgs/DeleteAffordanceTemplate.h>
#include <affordance_template_msgs/GetRunningAffordanceTemplates.h>
#include <affordance_template_msgs/AffordanceTemplatePlanCommand.h>
#include <affordance_template_msgs/AffordanceTemplateExecuteCommand.h>
#include <affordance_template_msgs/SaveAffordanceTemplate.h>
#include <affordance_template_msgs/AddAffordanceTemplateTrajectory.h>
#include <affordance_template_msgs/ScaleDisplayObject.h>
#include <affordance_template_msgs/ScaleDisplayObjectInfo.h>
#include <affordance_template_msgs/GetAffordanceTemplateStatus.h>
#include <affordance_template_msgs/GetAffordanceTemplateServerStatus.h>
#include <affordance_template_msgs/SetAffordanceTemplateTrajectory.h>
#include <affordance_template_msgs/SetAffordanceTemplatePose.h>

using namespace affordance_template_msgs;

namespace affordance_template_server
{
    class AffordanceTemplateInterface
    {
        // srv handlers
        bool handleRobotRequest(GetRobotConfigInfo::Request&, GetRobotConfigInfo::Response&);
        bool handleTemplateRequest(GetAffordanceTemplateConfigInfo::Request&, GetAffordanceTemplateConfigInfo::Response&);
        bool handleLoadRobot(LoadRobotConfig::Request&, LoadRobotConfig::Response&);
        bool handleAddTemplate(AddAffordanceTemplate::Request&, AddAffordanceTemplate::Response&);
        bool handleDeleteTemplate(DeleteAffordanceTemplate::Request&, DeleteAffordanceTemplate::Response&);
        bool handleRunning(GetRunningAffordanceTemplates::Request&, GetRunningAffordanceTemplates::Response&);
        bool handlePlanCommand(AffordanceTemplatePlanCommand::Request&, AffordanceTemplatePlanCommand::Response&);
        bool handleExecuteCommand(AffordanceTemplateExecuteCommand::Request&, AffordanceTemplateExecuteCommand::Response&);
        bool handleSaveTemplate(SaveAffordanceTemplate::Request&, SaveAffordanceTemplate::Response&);
        bool handleAddTrajectory(AddAffordanceTemplateTrajectory::Request&, AddAffordanceTemplateTrajectory::Response&);
        bool handleObjectScale(ScaleDisplayObject::Request&, ScaleDisplayObject::Response&);
        bool handleTemplateStatus(GetAffordanceTemplateStatus::Request&, GetAffordanceTemplateStatus::Response&);
        bool handleServerStatus(GetAffordanceTemplateServerStatus::Request&, GetAffordanceTemplateServerStatus::Response&);
        bool handleSetTrajectory(SetAffordanceTemplateTrajectory::Request&, SetAffordanceTemplateTrajectory::Response&);
        bool handleSetPose(SetAffordanceTemplatePose::Request&, SetAffordanceTemplatePose::Response&);
        
        void handleObjectScaleCallback(const ScaleDisplayObjectInfo&);
        bool getTemplateStatus(AffordanceTemplateStatus&, std::string, int, std::string, std::string);

        AffordanceTemplateServer* at_server_;
        tf::TransformListener listener_;
        std::map<std::string, ros::ServiceServer> at_srv_map_;

    public:
        AffordanceTemplateInterface(const std::string&);
        ~AffordanceTemplateInterface();
    };
        
}

#endif