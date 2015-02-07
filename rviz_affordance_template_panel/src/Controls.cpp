#include "Controls.hpp"

using namespace rviz_affordance_template_panel;
using namespace std;

Controls::Controls(Ui::RVizAffordanceTemplatePanel* ui) :
    ui_(ui) {}


bool Controls::requestPlan(Controls::CommandType command_type) {

    affordance_template_msgs::AffordanceTemplatePlanCommand srv;
    string key = ui_->control_template_box->currentText().toUtf8().constData();
    if(key=="") return false;

    ROS_INFO("Sending Plan command request for a %s", key.c_str());      

    vector<string> stuff = util::split(key, ':');
    srv.request.type = stuff[0];
    srv.request.id = int(atoi(stuff[1].c_str()));
    srv.request.trajectory_name = template_status_->getCurrentTrajectory();
    srv.request.backwards = (command_type==CommandType::STEP_BACKWARD);

    cout << "test 1 " <<endl;
    vector<pair<string,int> > ee_info = getSelectedEndEffectorInfo();


    cout << "test 2 " <<endl;
    for(auto &ee : ee_info) {
        srv.request.end_effectors.push_back(ee.first);
    }

    cout << "test 3 " <<endl;
   
    if(command_type==CommandType::CURRENT) {
        for(auto &ee : ee_info) {
            if (template_status_->getTrajectoryInfo().find(srv.request.trajectory_name) == template_status_->getTrajectoryInfo().end()) {
                ROS_ERROR("Controls::requestPlan() -- trajectory \'%s\' not found in template status", srv.request.trajectory_name.c_str());
                return false;
            }

            if (template_status_->getTrajectoryInfo()[srv.request.trajectory_name].find(ee.first) == template_status_->getTrajectoryInfo()[srv.request.trajectory_name].end()) {
                ROS_ERROR("Controls::requestPlan() -- end-effector \'%s\' not found in template status for traj \'%s\'", ee.first.c_str(),srv.request.trajectory_name.c_str());
                return false;
            }
            srv.request.steps.push_back(0);
        }
    } else if(command_type==CommandType::START || command_type==CommandType::END) {
     
        cout << "test 4 " <<endl;
    
        srv.request.direct = true;
        for(auto &ee : ee_info) {

            cout << "test 4a " <<endl;
            if (template_status_->getTrajectoryInfo().find(srv.request.trajectory_name) == template_status_->getTrajectoryInfo().end()) {
                ROS_ERROR("Controls::requestPlan() -- trajectory \'%s\' not found in template status", srv.request.trajectory_name.c_str());
                return false;
            }
            cout << "test 4b " <<endl;

            if (template_status_->getTrajectoryInfo()[srv.request.trajectory_name].find(ee.first) == template_status_->getTrajectoryInfo()[srv.request.trajectory_name].end()) {
                ROS_ERROR("Controls::requestPlan() -- end-effector \'%s\' not found in template status for traj \'%s\'", ee.first.c_str(),srv.request.trajectory_name.c_str());
                return false;
            }
            cout << "test 4c " <<endl;
            template_status_->getTrajectoryInfo()[srv.request.trajectory_name][ee.first];
            cout << "test 4d " <<endl;

            int idx = template_status_->getTrajectoryInfo()[srv.request.trajectory_name][ee.first]->waypoint_index;
            cout << "test 4e " <<endl;
            int N = template_status_->getTrajectoryInfo()[srv.request.trajectory_name][ee.first]->num_waypoints;
            cout << "test 4f " <<endl;
            int steps = 0;

            
            if(command_type==CommandType::START) {
                cout << "test 4a.a " <<endl;
                if(idx==-1) {
                    steps = 1;
                } else {
                    steps = idx;
                    srv.request.backwards = true;
                }
            } else if(command_type==CommandType::END) {
                cout << "test 4a.b " <<endl;
                if(idx==-1) {
                    steps = N;
                } else {
                    steps = N - idx - 1;
                    srv.request.backwards = false;
                }
            }
            srv.request.steps.push_back(steps);
            cout << "test 4c " <<endl;
                
        }
        cout << "test 4d " <<endl;
                
    } else {
        for(auto &ee : ee_info) {
            int steps = ui_->num_steps->text().toInt();
            srv.request.steps.push_back(steps);
        }
    }

    cout << "test 5 " <<endl;
                
    if (planService_.call(srv))
    {
        ROS_INFO("PLAN command successful, returned status: %d", (int)(srv.response.status)); // FIXME
        cout << "test 5a " <<endl;
        affordance_template_msgs::AffordanceTemplateStatusConstPtr ptr(new affordance_template_msgs::AffordanceTemplateStatus(srv.response.affordance_template_status));
        cout << "test 5b " <<endl;
        bool r = template_status_->updateTrajectoryStatus(ptr);
        if(!r) {
            ROS_ERROR("Controls::requestPlan() -- error updating template status");
        } 
        return r;
    }
    else
    {
        ROS_ERROR("Failed to call plan service command");
        return false;
    }
    cout << "test 6 " <<endl;
                
}


bool Controls::executePlan() {

    affordance_template_msgs::AffordanceTemplateExecuteCommand srv;
    string key = ui_->control_template_box->currentText().toUtf8().constData();
    if(key=="") return false;

    ROS_INFO("Sending Execute command request for a %s", key.c_str());      

    vector<string> stuff = util::split(key, ':');
    srv.request.type = stuff[0];
    srv.request.id = int(atoi(stuff[1].c_str()));
    srv.request.trajectory_name = template_status_->getCurrentTrajectory();
    
    vector<pair<string,int> > ee_info = getSelectedEndEffectorInfo();
    for(auto &ee : ee_info) {
        srv.request.end_effectors.push_back(ee.first);
    }

    if (executeService_.call(srv))
    {
        ROS_INFO("EXECUTE command successful, returned status: %d", (int)(srv.response.status)); // FIXME
        affordance_template_msgs::AffordanceTemplateStatusConstPtr ptr(new affordance_template_msgs::AffordanceTemplateStatus(srv.response.affordance_template_status));
        bool r = template_status_->updateTrajectoryStatus(ptr);
        if(!r) {
            ROS_ERROR("Controls::exsecutePlan() -- error updating template status");
        }
        return r;
    }
    else
    {
        ROS_ERROR("Failed to call execute service command");
        return false;
    }

}

vector<pair<string,int> > Controls::getSelectedEndEffectorInfo() {
    vector<pair<string,int> > selectedEndEffectorInfo;
    for (int r=0; r<ui_->end_effector_table->rowCount(); r++ ) {
        if (ui_->end_effector_table->item(r,1)->checkState() == Qt::Checked ) {
            string ee_name = ui_->end_effector_table->item(r,0)->text().toStdString();
            int ee_idx = ui_->end_effector_table->item(r,2)->text().toInt();
            pair<string,int> ee_info = make_pair(ee_name, ee_idx);
            selectedEndEffectorInfo.push_back(ee_info);
        }
    }
    return selectedEndEffectorInfo;
}
