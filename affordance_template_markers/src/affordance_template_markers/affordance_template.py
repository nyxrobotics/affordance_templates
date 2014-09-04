import yaml
import copy
import PyKDL as kdl
import tf
import threading

import rospy
import rospkg
import roslib; roslib.load_manifest("affordance_template_markers")

import geometry_msgs.msg
import sensor_msgs.msg
import visualization_msgs.msg

from interactive_markers.interactive_marker_server import *
from interactive_markers.menu_handler import *

from nasa_robot_teleop.moveit_interface import *

from affordance_template_markers.robot_config import *
from affordance_template_markers.frame_store import *
from affordance_template_markers.template_utilities import *
import affordance_template_markers.atdf_parser

class AffordanceTemplate(threading.Thread) :

    def __init__(self, server, id, name="affordance_template", initial_pose=None, robot_config=None):
        super(AffordanceTemplate,self).__init__()
        self.mutex = threading.Lock()
        self.menu_handler = MenuHandler()
        self.menu_handler.insert("Delete", callback=self.delete_callback)
        self.server = server
        self.frame_id = "world"
        self.id = int(id)
        self.key = name + ":" + str(self.id)
        self.name = name
        self.filename = ""
        self.root_object = ""
        self.parent_map = {}
        self.marker_map = {}
        self.callback_map = {}
        self.marker_pose_offset = {}
        self.display_objects = []
        self.frame_store_map = {}

        self.tf_listener = tf.TransformListener()
        self.tf_broadcaster = tf.TransformBroadcaster()
        self.tf_frame = "template:0"

        self.waypoints = []
        self.structure = None
        self.robot_config = None

        # parameter storage
        self.object_origin = {}
        self.object_controls = {}
        self.object_geometry = {}
        self.object_material = {}

        self.waypoint_origin = {}
        self.waypoint_controls = {}
        self.waypoint_end_effectors = {}
        self.waypoint_ids = {}
        self.waypoint_pose_map = {}

        # menu stuff
        self.marker_menus = {}
        self.menu_handles = {}

        # control helper stuff
        self.waypoint_index = {}
        self.waypoint_backwards_flag = {}
        self.waypoint_auto_execute = {}
        self.waypoint_plan_valid = {}
        self.waypoint_loop = {}
        self.waypoint_max = {}
        self.waypoint_controls_display_on = False
        # helper frames
        self.robotTroot = kdl.Frame()
        self.rootTobj = {}
        self.objTwp = {}
        self.wpTee = {}

        # not all templates will have a dynamic reconfigure server
        self.dserver = None
        self.initial_pose = initial_pose

        if not isinstance(robot_config, affordance_template_markers.robot_config.RobotConfig) :
            rospy.loginfo("AffordanceTemplate::init() -- problem setting robot config")
        else :
            self.robot_config = robot_config

        # set up menu info
        self.waypoint_menu_options = []
        self.waypoint_menu_options.append(("Display Next Path Segment", False))
        self.waypoint_menu_options.append(("Display Full Path", False))
        self.waypoint_menu_options.append(("Compute Backwards Path", True))
        # self.waypoint_menu_options.append(("Execute On Move", True))
        self.waypoint_menu_options.append(("Execute Next Segment", False))
        self.waypoint_menu_options.append(("Execute Full Path", False))
        self.waypoint_menu_options.append(("Loop Path", True))
        self.waypoint_menu_options.append(("Sync To Actual", False))
        self.waypoint_menu_options.append(("Manipulator Stored Poses", False))
        self.waypoint_menu_options.append(("End-Effector Stored Poses", False))
        self.waypoint_menu_options.append(("Hide Controls", True))
        self.waypoint_menu_options.append(("Add Waypoint Before", False))
        self.waypoint_menu_options.append(("Add Waypoint After", False))
        self.waypoint_menu_options.append(("Delete Waypoint", False))
        self.waypoint_menu_options.append(("Move Forward", False))
        self.waypoint_menu_options.append(("Move Back", False))

        self.object_menu_options = []
        self.object_menu_options.append(("Display Next Path Segment", False))
        self.object_menu_options.append(("Display Full Path", False))
        self.object_menu_options.append(("Compute Backwards Path", True))
        # self.object_menu_options.append(("Execute On Move", True))
        # self.object_menu_options.append(("Loop Path", True))
        self.object_menu_options.append(("Execute Next Segment", False))
        self.object_menu_options.append(("Execute Full Path", False))
        self.object_menu_options.append(("Add Waypoint Before", False))
        self.object_menu_options.append(("Add Waypoint After", False))
        self.object_menu_options.append(("Reset", False))
        self.object_menu_options.append(("Save", False))

        # start the frame update thread
        self.running = True
        self.start()

        rospy.loginfo("AffordanceTemplate::init() -- Done Creating new Empty AffordanceTemplate")

    def getPackagePath(self, pkg):
        """Return the path to the ROS package."""
        import rospkg
        try:
            rp = rospkg.RosPack()
            return rp.get_path(pkg)
        except:
            # print 'No package found: ', pkg
            return False

    def set_root_object(self, name) :
        self.root_object = name

    def get_root_object(self) :
        return self.root_object

    def add_interactive_marker(self, marker, callback=None):
        name = marker.name
        rospy.loginfo(str("Adding affordance template marker: " + name))
        self.marker_map[name] = marker
        if callback:
            self.callback_map[name] = callback
            return self.server.insert(marker, callback)
        else:
            self.callback_map[name] = self.process_feedback
            return self.server.insert(marker, self.process_feedback)

    def remove_interactive_marker(self, marker_name):
        if self.server.erase(marker_name):
            if marker_name in self.marker_map:
                del self.marker_map[marker_name]
                del self.callback_map[marker_name]
                return True
        return False

    def remove_all_markers(self) :
        markers = copy.deepcopy(self.marker_map)
        for m in markers :
            self.remove_interactive_marker(m)

    def attach_menu_handler(self, marker):
        return self.menu_handler.apply(self.server, marker.name)

    def get_marker(self):
        return self.server.get(self._key)

    def has_marker(self):
        if self._key in self.marker_map.keys():
            return True
        else:
            return False

    def delete_callback(self, feedback):
        for key in self.marker_map.keys():
            self.server.erase(key)
        self.server.applyChanges()
        # self.tear_down()

    def tear_down(self, keep_alive=False):
        # forcefully shut down service before killing node
        if self.dserver:
            self.dserver.set_service.shutdown("User deleted template.")
        # for rostest (and potentially other cases), we want to clean up but keep the node alive
        if not keep_alive:
            rospy.signal_shutdown("User deleted template.")

    def append_id(self, s) :
        return str(s + ":" + str(self.id))

    def is_parent(self, child, parent) :
        if not child in self.parent_map :
            return False
        elif self.parent_map[child] == None :
            return False
        elif self.parent_map[child] == parent:
            return True
        else :
            return self.is_parent(self.parent_map[child], parent)

    def get_chain(self, parent, child) :
        if not self.is_parent(child, parent) :
            return kdl.Frame()
        else :
            if parent == self.parent_map[child] :
                T = self.rootTobj[child]
            else :
                T = self.get_chain(parent,self.parent_map[child])*self.rootTobj[child]
            return T

    def get_chain_from_robot(self, child) :
        return self.get_chain("robot",child)

    def pose_from_origin(self, origin) :
        p = geometry_msgs.msg.Pose()
        p.orientation.w = 1
        try:
            q = (kdl.Rotation.RPY(origin.rpy[0],origin.rpy[1],origin.rpy[2])).GetQuaternion()
            p.position.x = origin.xyz[0]
            p.position.y = origin.xyz[1]
            p.position.z = origin.xyz[2]
            p.orientation.x = q[0]
            p.orientation.y = q[1]
            p.orientation.z = q[2]
            p.orientation.w = q[3]
        except :
            rospy.logerr("AffordanceTemplate::pose_from_origin() error")
        return p

    def load_initial_parameters(self) :

        objs = self.frame_store_map.keys()
        for k in objs: del self.frame_store_map[k]
        self.frame_store_map = {}

        if self.robot_config == None :
            rospy.error("AffordanceTemplate::load_initial_parameters() -- no robot config")
            return False

        if self.structure == None :
            rospy.error("AffordanceTemplate::load_initial_parameters() -- no structure")
            return False

        self.display_objects = []
        self.waypoints = []

        self.frame_id = self.robot_config.frame_id
        self.robotTroot = getFrameFromPose(self.robot_config.root_offset)

        self.name = self.structure.name
        self.key = self.name
        # self.key = self.append_id(self.name)

        # parse objects
        ids = 0
        for obj in self.structure.display_objects.display_objects :

            self.display_objects.append(obj.name)

            if ids == 0:
                self.set_root_object(obj.name)
                self.parent_map[obj.name] = "robot"

            if not obj.parent == None :
                self.parent_map[obj.name] = obj.parent

            self.rootTobj[obj.name] = getFrameFromPose(self.pose_from_origin(obj.origin))
            self.marker_pose_offset[obj.name] = self.pose_from_origin(obj.origin)

            self.object_origin[obj.name] = obj.origin
            self.object_controls[obj.name] = obj.controls
            self.object_geometry[obj.name] = obj.geometry
            self.object_material[obj.name] = obj.material

            ids += 1

        # parse end effector information
        wp_ids = 0
        for wp in self.structure.end_effector_waypoints.end_effector_waypoints :

            wp_name = self.append_id(str(wp.end_effector) + "." + str(wp.id))
            ee_name = self.robot_config.end_effector_name_map[int(wp.end_effector)]

            wp.display_object = wp.display_object
            if not wp.display_object in self.display_objects :
                rospy.logerr(str("AffordanceTemplate::create_from_structure() -- end-effector display object " + wp.display_object + "not found!"))

            if not wp.display_object == None :
                parent = wp.display_object
            else :
                parent = self.get_root_object()

            if wp.pose_group == None :
                pose_id = None
            else :
                pose_id = int(wp.pose_group.id)
            wp_pose = self.pose_from_origin(wp.origin)

            self.create_waypoint(int(wp.end_effector), int(wp.id), wp_pose, parent, wp.controls, wp.origin, pose_id)

            wp_ids += 1

    def load_initial_parameters_from_marker(self, m) :

        if self.robot_config == None :
            rospy.error("AffordanceTemplate::load_initial_parameters_from_marker() -- no robot config")
            return False

        self.display_objects = []
        self.waypoints = []

        self.frame_id = self.robot_config.frame_id
        self.robotTroot = getFrameFromPose(self.robot_config.root_offset)

        self.key = self.name + ":" + str(m.id)
        self.id = m.id
        # parse objects
        ids = 0
        self.display_objects.append(self.key)

        if ids == 0:
            self.set_root_object(self.key)
            self.parent_map[self.key] = "robot"

        ps = geometry_msgs.msg.PoseStamped()
        ps.header.frame_id = m.header.frame_id
        ps.pose = m.pose
        # ps.header.stamp = rospy.get_rostime()

        self.tf_listener.waitForTransform(ps.header.frame_id, self.frame_id, rospy.Time(0), rospy.Duration(5.0))
        ps = self.tf_listener.transformPose(self.frame_id, ps)

        robotTobj = getFrameFromPose(ps.pose)

        T = self.robotTroot.Inverse()*robotTobj

        self.rootTobj[self.key] = T
        ps.pose = getPoseFromFrame(T)
        self.marker_pose_offset[self.key] = ps.pose

        origin = self.create_origin_from_pose(ps.pose)
        controls = self.create_6dof_controls(0.25)

        geometry = None
        affordance_template_markers.atdf_parser.GeometricType()

        if m.type == Marker.MESH_RESOURCE : # affordance_template_markers.atdf_parser.Mesh
            geometry = affordance_template_markers.atdf_parser.Mesh()
            marker.mesh_resource = self.object_geometry[obj].filename
            geometry.scale[0] = m.scale.x
            geometry.scale[1] = m.scale.y
            geometry.scale[2] = m.scale.z
        elif m.type == Marker.CUBE : # affordance_template_markers.atdf_parser.Box) :
            geometry = affordance_template_markers.atdf_parser.Box()
            geometry.size = m.scale.x
        elif m.type == Marker.SPHERE: # affordance_template_markers.atdf_parser.Box) :
            geometry = affordance_template_markers.atdf_parser.Sphere()
            geometry.x = m.scale.x
            geometry.y = m.scale.y
            geometry.z = m.scale.z
        # elif m.type == Marker.SPHERE : # affordance_template_markers.atdf_parser.Sphere) :
        #     geometry.radius = m.scale.x
        elif m.type == Marker.CYLINDER : # affordance_template_markers.atdf_parser.Cylinder) :
            geometry = affordance_template_markers.atdf_parser.Cylinder()
            geometry.radius = m.scale.x
            geometry.length = m.scale.z

        material = affordance_template_markers.atdf_parser.Material()
        material.color = affordance_template_markers.atdf_parser.Color()
        material.color.rgba = [0]*4
        material.color.rgba[0] = m.color.r
        material.color.rgba[1] = m.color.g
        material.color.rgba[2] = m.color.b
        material.color.rgba[3] = m.color.a

        self.object_origin[self.key] = origin
        self.object_controls[self.key] = controls
        self.object_geometry[self.key] = geometry
        self.object_material[self.key] = material


    def create_from_parameters(self, keep_poses=False) :

        self.key = self.name
        self.frame_store_map[self.name] = FrameStore(self.key, self.robot_config.frame_id, getPoseFromFrame(self.robotTroot))

        # parse objects
        ids = 0
        debug_id = 0

        for obj in self.display_objects :

            int_marker = InteractiveMarker()
            control = InteractiveMarkerControl()

            self.marker_menus[obj] = MenuHandler()
            self.setup_object_menu(obj)

            root_frame = self.name
            obj_frame = obj

            if self.get_root_object() == obj :
                root_frame = self.key
            else :
                if obj in self.parent_map :
                     root_frame = self.parent_map[obj]

            int_marker.header.frame_id = str("/" + root_frame)
            int_marker.name = obj
            int_marker.description = obj
            int_marker.scale = self.object_controls[obj].scale

            control = InteractiveMarkerControl()
            marker = Marker()
            marker.ns = obj
            marker.id = ids

            if not keep_poses or not obj in self.frame_store_map.keys() :
                self.frame_store_map[obj] = FrameStore(obj_frame, root_frame, copy.deepcopy(self.marker_pose_offset[obj]))
                int_marker.pose = copy.deepcopy(self.marker_pose_offset[obj])
            else :
                int_marker.pose = copy.deepcopy(self.frame_store_map[obj].pose)

            if isinstance(self.object_geometry[obj], affordance_template_markers.atdf_parser.Mesh) :
                marker.type = Marker.MESH_RESOURCE
                marker.mesh_resource = self.object_geometry[obj].filename
                marker.mesh_use_embedded_materials = True
                marker.scale.x = self.object_geometry[obj].scale[0]
                marker.scale.y = self.object_geometry[obj].scale[1]
                marker.scale.z = self.object_geometry[obj].scale[2]
                # control = CreatePrimitiveControl(obj.name, p, 1.0, Marker.MESH_RESOURCE, ids)
            elif isinstance(self.object_geometry[obj], affordance_template_markers.atdf_parser.Box) :
                marker.type = Marker.CUBE
                marker.scale.x = self.object_geometry[obj].size[0]
                marker.scale.y = self.object_geometry[obj].size[1]
                marker.scale.z = self.object_geometry[obj].size[2]
            elif isinstance(self.object_geometry[obj], affordance_template_markers.atdf_parser.Sphere) :
                marker.type = Marker.SPHERE
                marker.scale.x = self.object_geometry[obj].x
                marker.scale.y = self.object_geometry[obj].y
                marker.scale.z = self.object_geometry[obj].z
            elif isinstance(self.object_geometry[obj], affordance_template_markers.atdf_parser.Cylinder) :
                marker.type = Marker.CYLINDER
                marker.scale.x = self.object_geometry[obj].radius
                marker.scale.y = self.object_geometry[obj].radius
                marker.scale.z = self.object_geometry[obj].length

            control.markers.append(marker)

            if obj in self.object_material :
                if self.object_material[obj] != None :
                    control.markers[0].color.r = self.object_material[obj].color.rgba[0]
                    control.markers[0].color.g = self.object_material[obj].color.rgba[1]
                    control.markers[0].color.b = self.object_material[obj].color.rgba[2]
                    control.markers[0].color.a = self.object_material[obj].color.rgba[3]
                    if isinstance(self.object_geometry[obj], affordance_template_markers.atdf_parser.Mesh) :
                        control.markers[0].mesh_use_embedded_materials = False

            scale = 1.0
            if obj in self.object_controls :
                scale = self.object_controls[obj]

            # int_marker = CreateInteractiveMarker(self.frame_id, obj.name, scale)
            int_marker.controls.append(control)
            int_marker.controls.extend(CreateCustomDOFControls("",
                self.object_controls[obj].xyz[0], self.object_controls[obj].xyz[1], self.object_controls[obj].xyz[2],
                self.object_controls[obj].rpy[0], self.object_controls[obj].rpy[1], self.object_controls[obj].rpy[2]))

            self.add_interactive_marker(int_marker)
            self.marker_menus[obj].apply( self.server, obj )
            self.server.applyChanges()

            self.marker_map[obj] = control.markers[0]
            self.marker_pose_offset[obj] = self.pose_from_origin(self.object_origin[obj])

            ids += 1

        # return
        # parse end effector information
        wp_ids = 0
        for wp in self.waypoints :

            # print "creating marker for wp: ", wp
            ee_name = self.robot_config.end_effector_name_map[int(self.waypoint_end_effectors[wp])]

            root_frame = self.name #str(self.name + ":" + str(self.id))
            if wp in self.parent_map :
                root_frame = self.parent_map[wp] #str(self.parent_map[wp] + ":" + str(self.id))

                if not self.parent_map[wp] in self.display_objects :
                    rospy.logerr(str("AffordanceTemplate::create_from_parameters() -- end-effector display object " + str(self.parent_map[wp]) + "not found!"))

            display_pose = getPoseFromFrame(self.objTwp[wp])

            int_marker = InteractiveMarker()
            control = InteractiveMarkerControl()

            int_marker.header.frame_id = str("/" + root_frame)
            int_marker.pose = display_pose
            int_marker.name = wp
            int_marker.description = wp
            int_marker.scale = self.waypoint_controls[wp].scale

            menu_control = InteractiveMarkerControl()
            menu_control.interaction_mode = InteractiveMarkerControl.BUTTON

            self.marker_menus[wp] = MenuHandler()
            self.setup_waypoint_menu(wp, ee_name)

            id = int(self.waypoint_end_effectors[wp])
            if self.waypoint_backwards_flag[id] :
                self.marker_menus[wp].setCheckState( self.menu_handles[(wp,"Compute Backwards Path")], MenuHandler.CHECKED )
            if self.waypoint_auto_execute[id] :
                self.marker_menus[wp].setCheckState( self.menu_handles[(wp,"Execute On Move")], MenuHandler.CHECKED )
            if self.waypoint_loop[id] :
                self.marker_menus[wp].setCheckState( self.menu_handles[(wp,"Loop Path")], MenuHandler.CHECKED )
            if self.waypoint_controls_display_on :
                self.marker_menus[wp].setCheckState( self.menu_handles[(wp,"Hide Controls")], MenuHandler.UNCHECKED )
            else :
                self.marker_menus[wp].setCheckState( self.menu_handles[(wp,"Hide Controls")], MenuHandler.CHECKED )

            # ask for end_effector pose markers here (and change ids/colors as necessary)
            id = self.waypoint_pose_map[wp]
            if id == None :
                pn = "current"
            else :
                pn = self.robot_config.end_effector_id_map[ee_name][id]

            # print "##########getting markers for pose ", pn
            markers = self.robot_config.end_effector_link_data[ee_name].get_markers_for_pose(pn)
            for m in markers.markers :
            # for m in self.robot_config.end_effector_markers[ee_name].markers :
                # print "copying marker: ", m.text
                ee_m = copy.deepcopy(m)
                ee_m.header.frame_id = ""
                ee_m.pose = getPoseFromFrame(self.wpTee[wp]*getFrameFromPose(m.pose))
                # ee_m.color.r = .2
                # ee_m.color.g = .5
                # ee_m.color.b = .2
                # ee_m.color.a = .8
                menu_control.markers.append( ee_m )

            scale = 1.0
            if wp in self.waypoint_controls :
                scale = self.waypoint_controls[wp].scale
            int_marker.controls.append(menu_control)
            if(self.waypoint_controls_display_on) :
                int_marker.controls.extend(CreateCustomDOFControls("",
                    self.waypoint_controls[wp].xyz[0], self.waypoint_controls[wp].xyz[1], self.waypoint_controls[wp].xyz[2],
                    self.waypoint_controls[wp].rpy[0], self.waypoint_controls[wp].rpy[1], self.waypoint_controls[wp].rpy[2]))

            self.add_interactive_marker(int_marker)
            self.marker_menus[wp].apply( self.server, wp )
            self.server.applyChanges()

            wp_ids += 1


    def update_template_defaults(self, objects=True, waypoints=True) :

        if objects :
            for obj in self.object_origin.keys() :
                current_pose = self.frame_store_map[obj].pose
                xyz = [current_pose.position.x, current_pose.position.y, current_pose.position.z]
                rpy = (kdl.Rotation.Quaternion(current_pose.orientation.x,current_pose.orientation.y,current_pose.orientation.z,current_pose.orientation.w)).GetRPY()
                for i in range(3) :
                    self.object_origin[obj].xyz[i] = xyz[i]
                    self.object_origin[obj].rpy[i] = rpy[i]

        if waypoints :
            for wp in self.waypoints :
                current_pose = getPoseFromFrame(self.objTwp[wp])
                xyz = [current_pose.position.x, current_pose.position.y, current_pose.position.z]
                rpy = (kdl.Rotation.Quaternion(current_pose.orientation.x,current_pose.orientation.y,current_pose.orientation.z,current_pose.orientation.w)).GetRPY()
                for i in range(3) :
                    self.waypoint_origin[wp].xyz[i] = xyz[i]
                    self.waypoint_origin[wp].rpy[i] = rpy[i]


    def save_to_disk(self, filename=None, package=None) :

        name = self.structure.name.split(":")[0]

        if filename == None :
            filename = name + str(".atdf")

        filename_bak = filename + str(".bak")
        if package == None:
            package = "affordance_template_library"

        # get package and file path
        import os
        package_path = self.getPackagePath(package)
        template_path    = os.path.join(package_path, 'templates')
        output_file = os.path.join(template_path, filename)

        # backup file
        try :
            import shutil
            dstname = os.path.join(template_path, filename_bak)
            print "copy name: ", dstname
            shutil.copyfile(output_file,dstname)
        except :
            rospy.loginfo("AffordanceTemplate::save_to_disk() -- no file to backup")

        # set current object and waypoint positions to "default"
        self.update_template_defaults()

        # create xml file
        import xml.etree.cElementTree as ET

        root = ET.Element("affordance_template")
        # root.set('version', '1.0')
        root.set("name", name)
        root.set("image", self.structure.image)

        display_objects = ET.SubElement(root, "display_objects")
        for obj in self.display_objects :
            key = obj.split(":")[0]
            obj_element = ET.SubElement(display_objects, "display_object")
            obj_element.set("name", key)
            if obj in self.parent_map:
                if not self.parent_map[obj] == "robot" :
                    parent_key = self.parent_map[obj].split(":")[0]
                    obj_element.set("parent", parent_key)

            print self.object_geometry[obj]
            if isinstance(self.object_geometry[obj], affordance_template_markers.atdf_parser.Mesh) :
                geometry_element = ET.SubElement(obj_element, "geometry")
                mesh_element = ET.SubElement(geometry_element, "mesh")
                mesh_element.set("filename", self.object_geometry[obj].filename)
                mesh_element.set("scale", str(str(self.object_geometry[obj].scale[0]) + " " + str(self.object_geometry[obj].scale[1]) + " " + str(self.object_geometry[obj].scale[2])))
            elif isinstance(self.object_geometry[obj], affordance_template_markers.atdf_parser.Box) :
                geometry_element = ET.SubElement(obj_element, "geometry")
                box_element = ET.SubElement(geometry_element, "box")
                box_element.set("size", str(str(self.object_geometry[obj].size) + " " + str(self.object_geometry[obj].size) + " " + str(self.object_geometry[obj].size)))
            elif isinstance(self.object_geometry[obj], affordance_template_markers.atdf_parser.Sphere):
                geometry_element = ET.SubElement(obj_element, "geometry")
                sphere_element = ET.SubElement(geometry_element, "sphere")
                sphere_element.set("radius", str(self.object_geometry[obj].x))
            elif isinstance(self.object_geometry[obj], affordance_template_markers.atdf_parser.Cylinder):
                geometry_element = ET.SubElement(obj_element, "geometry")
                cylinder_element = ET.SubElement(geometry_element, "cylinder")
                cylinder_element.set("radius", str(self.object_geometry[obj].radius))
                cylinder_element.set("length", str(self.object_geometry[obj].length))

            if not self.object_material[obj] == None :
                material_element = ET.SubElement(obj_element, "material")
                material_element.set("name", self.object_material[obj].name)
                color_element = ET.SubElement(material_element, "color")
                color_element.set("rgba", str(str(self.object_material[obj].color.rgba[0]) + " " + str(self.object_material[obj].color.rgba[1])
                        + " " + str(self.object_material[obj].color.rgba[2]) + " " + str(self.object_material[obj].color.rgba[3])))

            origin_element = ET.SubElement(obj_element, "origin")
            origin_element.set("xyz", str(str(self.object_origin[obj].xyz[0]) + " " + str(self.object_origin[obj].xyz[1]) + " " + str(self.object_origin[obj].xyz[2])))
            origin_element.set("rpy", str(str(self.object_origin[obj].rpy[0]) + " " + str(self.object_origin[obj].rpy[1]) + " " + str(self.object_origin[obj].rpy[2])))

            controls_element = ET.SubElement(obj_element, "controls")
            controls_element.set("xyz", str(str(int(self.object_controls[obj].xyz[0])) + " " + str(int(self.object_controls[obj].xyz[1])) + " " + str(int(self.object_controls[obj].xyz[2]))))
            controls_element.set("rpy", str(str(int(self.object_controls[obj].rpy[0])) + " " + str(int(self.object_controls[obj].rpy[1])) + " " + str(int(self.object_controls[obj].rpy[2]))))
            controls_element.set("scale", str(self.object_controls[obj].scale))


        waypoints = ET.SubElement(root, "end_effector_waypoints")
        for wp in self.waypoints :
            ee = wp.split(".")[0]
            id = wp.split(".")[1].split(":")[0]
            parent_key = self.parent_map[wp].split(":")[0]

            wp_element = ET.SubElement(waypoints, "end_effector_waypoint")
            wp_element.set("end_effector", ee)
            wp_element.set("id", id)
            wp_element.set("display_object", parent_key)

            origin_element = ET.SubElement(wp_element, "origin")
            origin_element.set("xyz", str(str(self.waypoint_origin[wp].xyz[0]) + " " + str(self.waypoint_origin[wp].xyz[1]) + " " + str(self.waypoint_origin[wp].xyz[2])))
            origin_element.set("rpy", str(str(self.waypoint_origin[wp].rpy[0]) + " " + str(self.waypoint_origin[wp].rpy[1]) + " " + str(self.waypoint_origin[wp].rpy[2])))

            controls_element = ET.SubElement(wp_element, "controls")
            controls_element.set("xyz", str(str(int(self.waypoint_controls[wp].xyz[0])) + " " + str(int(self.waypoint_controls[wp].xyz[1])) + " " + str(int(self.waypoint_controls[wp].xyz[2]))))
            controls_element.set("rpy", str(str(int(self.waypoint_controls[wp].rpy[0])) + " " + str(int(self.waypoint_controls[wp].rpy[1])) + " " + str(int(self.waypoint_controls[wp].rpy[2]))))
            controls_element.set("scale", str(self.waypoint_controls[wp].scale))


        rospy.loginfo("AffordanceTemplate::save_to_disk() -- writing file to disk...")
        # print output_file

        def pretty_print_xml(xml):
            import subprocess
            proc = subprocess.Popen(
                ['xmllint', '--format', '/dev/stdin'],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
            )
            (output, error_output) = proc.communicate(xml);
            return output

        xmlstr = ET.tostring(root)
        import io
        f = io.open(output_file, "wb")
        f.write(pretty_print_xml(xmlstr))
        f.close()


    def create_waypoint(self, ee_id, wp_id, ps, parent, controls=None, origin=None, pose_id=None) :

        wp_name = str(ee_id) + "." + str(wp_id) + ":" + str(self.id)
        ee_name = self.robot_config.end_effector_name_map[ee_id]

        self.parent_map[wp_name] = parent

        self.objTwp[wp_name] = getFrameFromPose(ps)

        ee_offset = self.robot_config.manipulator_pose_map[ee_name]
        self.wpTee[wp_name] = getFrameFromPose(ee_offset)

        if controls == None : controls = self.create_6dof_controls(0.25)
        self.waypoint_controls[wp_name] = controls

        if origin == None : origin = self.create_origin_from_pose(ps)
        self.waypoint_origin[wp_name] = origin

        self.waypoint_end_effectors[wp_name] = ee_id
        self.waypoint_ids[wp_name] = wp_id

        self.waypoint_pose_map[wp_name] = pose_id

        if ee_id not in self.waypoint_max: self.waypoint_max[ee_id] = 0
        if self.waypoint_end_effectors[wp_name] not in self.waypoint_index :
            self.waypoint_index[ee_id] = -1
            self.waypoint_backwards_flag[ee_id] = False
            self.waypoint_auto_execute[ee_id] = False
            self.waypoint_plan_valid[ee_id] = False
            self.waypoint_loop[ee_id] = False
        else :
            if int(self.waypoint_ids[wp_name]) > self.waypoint_max[ee_id] :
                self.waypoint_max[ee_id] = int(self.waypoint_ids[wp_name])

        if not wp_name in self.waypoints :
            self.waypoints.append(wp_name)
            self.waypoints.sort()

    def remove_waypoint(self, ee_id, wp_id) :
        max_idx = self.waypoint_max[ee_id]
        for k in range(wp_id, max_idx) : self.move_waypoint(ee_id, k+1, k)
        last_wp_name = str(ee_id) + "." + str(self.waypoint_max[ee_id]) + ":" + str(self.id)
        del self.parent_map[last_wp_name]
        del self.objTwp[last_wp_name]
        del self.wpTee[last_wp_name]
        del self.waypoint_controls[last_wp_name]
        del self.waypoint_origin[last_wp_name]
        del self.waypoint_end_effectors[last_wp_name]
        del self.waypoint_ids[last_wp_name]
        del self.waypoint_pose_map[last_wp_name]
        self.waypoint_max[ee_id] -= 1
        while last_wp_name in self.waypoints: self.waypoints.remove(last_wp_name)
        self.waypoints.sort()
        self.remove_interactive_marker(last_wp_name)

    def move_waypoint(self, ee_id, old_id, new_id) :
        old_name = str(ee_id) + "." + str(old_id) + ":" + str(self.id)
        new_name = str(ee_id) + "." + str(new_id) + ":" + str(self.id)
        self.create_waypoint(ee_id, new_id, getPoseFromFrame(self.objTwp[old_name]), self.parent_map[old_name], self.waypoint_controls[old_name], self.waypoint_origin[old_name], self.waypoint_pose_map[old_name])

    def swap_waypoints(self, ee_id, wp_id1, wp_id2) :
        wp_name1 = str(ee_id) + "." + str(wp_id1) + ":" + str(self.id)
        wp_name2 = str(ee_id) + "." + str(wp_id2) + ":" + str(self.id)

        objTwp1 = self.objTwp[wp_name1]
        objTwp2 = self.objTwp[wp_name2]

        parent1 = self.parent_map[wp_name1]
        parent2 = self.parent_map[wp_name2]

        pose_map1 = self.waypoint_pose_map[wp_name1]
        pose_map2 = self.waypoint_pose_map[wp_name2]

        controls1 = self.waypoint_controls[wp_name1]
        controls2 = self.waypoint_controls[wp_name2]

        origin1 = self.waypoint_origin[wp_name1]
        origin2 = self.waypoint_origin[wp_name2]

        self.create_waypoint(ee_id, wp_id1, getPoseFromFrame(objTwp2), parent2, controls2, origin2, pose_map2)
        self.create_waypoint(ee_id, wp_id2, getPoseFromFrame(objTwp1), parent1, controls1, origin1, pose_map1)

    def create_from_structure(self) :
        rospy.loginfo("AffordanceTemplate::create_from_structure() -- loading initial parameters")
        self.load_initial_parameters()
        rospy.loginfo("AffordanceTemplate::create_from_structure() -- creating from parameters")
        self.create_from_parameters()
        rospy.loginfo("AffordanceTemplate::create_from_structure() -- done")
        # self.print_structure()

    def setup_object_menu(self, obj) :
        for m,c in self.object_menu_options :
            if m == "Add Waypoint Before" or m == "Add Waypoint After":
                sub_menu_handle = self.marker_menus[obj].insert(m)
                for ee in self.robot_config.end_effector_name_map.iterkeys() :
                    name = self.robot_config.end_effector_name_map[ee]
                    self.menu_handles[(obj,m,name)] = self.marker_menus[obj].insert(name,parent=sub_menu_handle,callback=self.create_waypoint_callback)
            else :
                self.menu_handles[(obj,m)] = self.marker_menus[obj].insert( m, callback=self.process_feedback )
                if c : self.marker_menus[obj].setCheckState( self.menu_handles[(obj,m)], MenuHandler.UNCHECKED )

    def setup_waypoint_menu(self, waypoint, group) :
        for m,c in self.waypoint_menu_options :
            if m == "Manipulator Stored Poses" :
                sub_menu_handle = self.marker_menus[waypoint].insert(m)
                parent_group = self.robot_config.moveit_interface.srdf_model.get_end_effector_parent_group(group)
                for p in self.robot_config.moveit_interface.get_stored_state_list(parent_group) :
                    self.menu_handles[(waypoint,m,p)] = self.marker_menus[waypoint].insert(p,parent=sub_menu_handle,callback=self.stored_pose_callback)
            elif m == "End-Effector Stored Poses" :
                sub_menu_handle = self.marker_menus[waypoint].insert(m)
                for p in self.robot_config.moveit_interface.get_stored_state_list(group) :
                    self.menu_handles[(waypoint,m,p)] = self.marker_menus[waypoint].insert(p,parent=sub_menu_handle,callback=self.stored_pose_callback)
            else :
                self.menu_handles[(waypoint,m)] = self.marker_menus[waypoint].insert( m, callback=self.process_feedback )
                if c : self.marker_menus[waypoint].setCheckState( self.menu_handles[(waypoint,m)], MenuHandler.UNCHECKED )

    def load_from_file(self, filename) :
        print "AffordanceTemplate::load_from_file() -- building structure"
        self.structure = affordance_template_markers.atdf_parser.AffordanceTemplateStructure.from_file(filename)
        self.structure = self.append_id_to_structure(self.structure)
        print "AffordanceTemplate::load_from_file() -- loading initial parameters"
        self.load_initial_parameters()
        print "AffordanceTemplate::load_from_file() -- creating RViz template from parameters"
        self.create_from_parameters()
        print "AffordanceTemplate::load_from_file() -- done"
        # self.print_structure()
        stuff = filename.split("/")
        self.filename = stuff[len(stuff)-1]

        return self.structure

    def load_from_marker(self, m) :
        print "AffordanceTemplate::load_from_marker() -- building structure"
        print "AffordanceTemplate::load_from_marker() -- loading initial parameters"
        self.load_initial_parameters_from_marker(m)
        print "AffordanceTemplate::load_from_marker() -- creating RViz template from parameters"
        self.create_from_parameters()
        print "AffordanceTemplate::load_from_marker() -- done"
        # self.print_structure()
        return self.structure

    def append_id_to_structure(self, structure) :
        structure.name = self.append_id(structure.name)
        for obj in structure.display_objects.display_objects :
            obj.name = self.append_id(obj.name)
            if not obj.parent == None :
                obj.parent = self.append_id(obj.parent)
                # print "appending id to parent ", self.parent_map[obj.name], " of ", obj.name

        for wp in structure.end_effector_waypoints.end_effector_waypoints :
            wp.display_object = self.append_id(wp.display_object)

        return structure

    def print_structure(self) :
        print "---------------------"
        print "---------------------"
        print "found new template:"
        print "  name:  ", self.structure.name
        print "  image: ", self.structure.image
        for obj in self.structure.display_objects.display_objects :
            print "---------------------"
            print "\tobject: ", obj.name
            if not obj.parent == None :
                print "\tparent: ", obj.parent

            print "----------------"
            print "\torigin xyz: ", obj.origin.xyz
            print "\torigin rpy: ", obj.origin.rpy
            print "----------------"
            print "\tcontrols xyz: ", obj.controls.xyz
            print "\tcontrols rpy: ", obj.controls.rpy
            print "----------------"
            if isinstance(obj.geometry, affordance_template_markers.atdf_parser.Box) :
                print "\tBox size: ", obj.geometry.size
            if isinstance(obj.geometry, affordance_template_markers.atdf_parser.Sphere) :
                print "\tSphere radius x: ", obj.geometry.x
                print "\tSphere radius y: ", obj.geometry.y
                print "\tSphere radius z: ", obj.geometry.z
            if isinstance(obj.geometry, affordance_template_markers.atdf_parser.Cylinder) :
                print "\tCylinder size: (", obj.geometry.radius, ", ", obj.geometry.length, ")"
            if isinstance(obj.geometry, affordance_template_markers.atdf_parser.Mesh) :
                print "\tMesh: ", obj.geometry.filename
            if not obj.material == None :
                print "\tmaterial: ", obj.material.name

        print "---------------------"
        for wp in self.structure.end_effector_waypoints.end_effector_waypoints :
            print "---------------------"
            print "\twaypoint: ", wp.end_effector, ".", wp.id , ":", str(self.id)
            print "----------------"
            print "\tdisplay_object: ", wp.display_object
            print "----------------"
            print "\torigin xyz: ", wp.origin.xyz
            print "\torigin rpy: ", wp.origin.rpy
            print "----------------"
            print "\tcontrols xyz: ", wp.controls.xyz
            print "\tcontrols rpy: ", wp.controls.rpy
            print "----------------"
            if not wp.pose_group == None :
                print "\tee pose group id: ", wp.pose_group.id

    def process_feedback(self, feedback):
        # print "\n--------------------------------"
        # print "Process Feedback on marker: ", feedback.marker_name
        # print feedback.pose

        if feedback.marker_name in self.display_objects :
            self.frame_store_map[feedback.marker_name].pose = feedback.pose

        if feedback.event_type == InteractiveMarkerFeedback.MOUSE_UP :

            if feedback.marker_name in self.waypoints :
                self.objTwp[feedback.marker_name] = getFrameFromPose(feedback.pose)

        elif feedback.event_type == InteractiveMarkerFeedback.MENU_SELECT:

            handle = feedback.menu_entry_id
            print "--------------"

            if feedback.marker_name in self.display_objects :
                ee_list = self.waypoint_index.keys()
                if handle == self.menu_handles[(feedback.marker_name,"Reset")] :
                    rospy.loginfo(str("AffordanceTemplate::process_feedback() -- Reseting Affordance Template"))
                    self.create_from_structure()

                if handle == self.menu_handles[(feedback.marker_name,"Save")] :
                    rospy.loginfo(str("AffordanceTemplate::process_feedback() -- Saving Affordance Template"))
                    self.save_to_disk(self.filename)

            else :
                ee_list =[int(feedback.marker_name.split(".")[0])]
                waypoint_id = int(feedback.marker_name.split(".")[1].split(":")[0])

            for ee_id in ee_list :

                # print "Waypoint Menu\n--------------------"

                ee_name = self.robot_config.get_end_effector_name(ee_id)
                manipulator_name = self.robot_config.get_manipulator(ee_name)
                ee_offset = self.robot_config.manipulator_pose_map[ee_name]
                max_idx = self.waypoint_max[ee_id]

                # print "ee_name: ", ee_name
                # print "ee_id: ", ee_id
                # print "max wp ", max_idx
                # print "selected waypoint id: ", waypoint_id
                # print "stored waypoint idx: ", self.waypoint_index[ee_id]
                # print "manipulator_name: ", manipulator_name

                next_path_idx = self.compute_next_path_id(ee_id, 1, self.waypoint_backwards_flag[ee_id])

                if handle == self.menu_handles[(feedback.marker_name,"Display Next Path Segment")] :
                    next_path_str = str(str(ee_id) + "." + str(next_path_idx)) + ":" + str(self.id)
                    if not next_path_str in self.objTwp :
                        rospy.logerr(str("AffordanceTemplate::process_feedback() -- path index[" + str(next_path_str) + "] not found!!"))
                    else :
                        rospy.loginfo(str("AffordanceTemplate::process_feedback() -- computing path to index[" + str(next_path_str) + "]"))
                        k = str(next_path_str)
                        pt = geometry_msgs.msg.PoseStamped()
                        pt.header = self.server.get(k).header
                        pt.pose = self.server.get(k).pose

                        T_goal = getFrameFromPose(pt.pose)
                        T_offset = getFrameFromPose(ee_offset)
                        T = T_goal*T_offset
                        pt.pose = getPoseFromFrame(T)

                        self.robot_config.moveit_interface.groups[manipulator_name].clear_pose_targets()
                        self.robot_config.moveit_interface.create_plan_to_target(manipulator_name, pt)

                        if not self.waypoint_pose_map[next_path_str] == None :
                            id = self.waypoint_pose_map[next_path_str]
                            pn = self.robot_config.end_effector_id_map[ee_name][id]
                            self.robot_config.moveit_interface.create_joint_plan_to_target(ee_name, self.robot_config.stored_poses[ee_name][pn])

                        self.waypoint_plan_valid[ee_id] = True

                if handle == self.menu_handles[(feedback.marker_name,"Display Full Path")] :
                    waypoints = []
                    frame_id = ""
                    # print "waypoint_index: ", self.waypoint_index[ee_id]
                    # print "next_path_idx: ", next_path_idx
                    # print "max_idx: ", max_idx

                    r = range(0, max_idx+1)
                    if self.waypoint_backwards_flag[ee_id] :
                        r.reverse()

                    for idx in r :
                        next_path_str = str(str(ee_id) + "." + str(idx)) + ":" + str(self.id)
                        if not next_path_str in self.objTwp :
                            rospy.logerr(str("AffordanceTemplate::process_feedback() -- path index[" + str(next_path_str) + "] not found!!"))
                        else :
                            rospy.loginfo(str("AffordanceTemplate::process_feedback() -- computing path to index[" + str(next_path_str) + "]"))
                            k = str(next_path_str)
                            pt = geometry_msgs.msg.PoseStamped()
                            pt.header = self.server.get(k).header
                            pt.pose = self.server.get(k).pose
                            frame_id =  pt.header.frame_id

                            T_goal = getFrameFromPose(pt.pose)
                            T_offset = getFrameFromPose(ee_offset)
                            T = T_goal*T_offset
                            pt.pose = getPoseFromFrame(T)
                            waypoints.append(pt.pose)
                    self.robot_config.moveit_interface.create_path_plan(manipulator_name, frame_id, waypoints)
                    self.waypoint_plan_valid[ee_id] = True

                if handle == self.menu_handles[(feedback.marker_name,"Execute Next Segment")] :
                    if self.waypoint_plan_valid[ee_id] :

                        # move the manipulator (arm)
                        r = self.robot_config.moveit_interface.execute_plan(manipulator_name,from_stored=True, wait=False)
                        if not r :
                            rospy.logerr(str("RobotTeleop::process_feedback(mouse) -- failed moveit execution for group: " + manipulator_name + ". re-synching..."))

                        # move the end_effector (hand)
                        r = self.robot_config.moveit_interface.execute_plan(ee_name,from_stored=True)
                        if not r :
                            rospy.logerr(str("RobotTeleop::process_feedback(mouse) -- failed moveit execution for group: " + ee_name + ". re-synching..."))

                        # move to end effector pose if there is one
                        # r = self.robot_config.moveit_interface.execute_plan(end_effector_name,from_stored=True)
                        # if not r :
                        #     rospy.logerr(str("RobotTeleop::process_feedback(mouse) -- failed moveit execution for group: " + end_effector_name + ". re-synching..."))

                        self.waypoint_index[ee_id] = next_path_idx
                        rospy.loginfo(str("setting current waypoint idx: " + str(self.waypoint_index[ee_id])))
                        self.waypoint_plan_valid[ee_id] = False

                if handle == self.menu_handles[(feedback.marker_name,"Execute Full Path")] :
                    if self.waypoint_plan_valid[ee_id] :
                        r = self.robot_config.moveit_interface.execute_plan(manipulator_name,from_stored=True)
                        if not r :
                            rospy.logerr(str("RobotTeleop::process_feedback(mouse) -- failed moveit execution for group: " + manipulator_name + ". re-synching..."))
                        rospy.loginfo(str("setting current waypoint idx: " + str(self.waypoint_index[ee_id])))
                        self.waypoint_plan_valid[ee_id] = False


                if handle == self.menu_handles[(feedback.marker_name,"Compute Backwards Path")] :
                    state = self.marker_menus[feedback.marker_name].getCheckState( handle )
                    if state == MenuHandler.CHECKED:
                        self.marker_menus[feedback.marker_name].setCheckState( handle, MenuHandler.UNCHECKED )
                        self.waypoint_backwards_flag[ee_id] = False
                    else :
                        self.marker_menus[feedback.marker_name].setCheckState( handle, MenuHandler.CHECKED )
                        self.waypoint_backwards_flag[ee_id] = True
                    rospy.loginfo(str("AffordanceTemplate::process_feedback() -- setting Backwards Path flag for ee[" + str(ee_id) + "] to " + str(self.waypoint_backwards_flag[ee_id])))

                if handle == self.menu_handles[(feedback.marker_name,"Add Waypoint Before")] :
                    print "Adding Waypoint before ", waypoint_id, "for end effector: ", ee_id

                    new_pose = geometry_msgs.msg.Pose()
                    first_name = str(str(ee_id) + "." + str(waypoint_id) + ":" + str(self.id))
                    pose_first = getPoseFromFrame(self.objTwp[first_name])
                    new_id = 0
                    if waypoint_id > 0 :
                        new_id = waypoint_id-1
                        second_name = str(str(ee_id) + "." + str(new_id) + ":" + str(self.id))
                        pose_second = getPoseFromFrame(self.objTwp[second_name])
                        new_pose.position.x = (pose_second.position.x - pose_first.position.x)/2.0 + pose_first.position.x
                        new_pose.position.y = (pose_second.position.y - pose_first.position.y)/2.0 + pose_first.position.y
                        new_pose.position.z = (pose_second.position.z - pose_first.position.z)/2.0 + pose_first.position.z
                        new_pose.orientation = copy.deepcopy(pose_first.orientation)
                    else :
                        new_pose = copy.deepcopy(pose_first)
                        new_pose.position.x +=0.025
                        new_pose.position.y +=0.025
                        new_pose.position.z +=0.025

                    r = range(new_id,max_idx+1)
                    r.reverse()
                    for k in r:
                        old_name = str(ee_id) + "." + str(k) + ":" + str(self.id)
                        new_name = str(ee_id) + "." + str(k+1) + ":" + str(self.id)
                        self.move_waypoint(ee_id, k, k+1)

                    # print "creating waypoint at : ", new_id
                    old_name = str(ee_id) + "." + str(1)  + ":" + str(self.id)
                    self.create_waypoint(ee_id, waypoint_id, new_pose, self.parent_map[old_name], self.waypoint_controls[old_name], self.waypoint_origin[old_name], self.waypoint_pose_map[old_name]) # is parent right here?
                    self.create_from_parameters(True)

                if handle == self.menu_handles[(feedback.marker_name,"Add Waypoint After")] :
                    print "Adding Waypoint after ", waypoint_id, "for end effector: ", ee_id

                    new_pose = geometry_msgs.msg.Pose()
                    first_name = str(ee_id) + "." + str(waypoint_id) + ":" + str(self.id)
                    pose_first = getPoseFromFrame(self.objTwp[first_name])
                    new_id = waypoint_id+1
                    if waypoint_id < max_idx :
                        second_name = str(str(ee_id) + "." + str(new_id) + ":" + str(self.id))
                        pose_second = getPoseFromFrame(self.objTwp[second_name])
                        new_pose.position.x = (pose_second.position.x - pose_first.position.x)/2.0 + pose_first.position.x
                        new_pose.position.y = (pose_second.position.y - pose_first.position.y)/2.0 + pose_first.position.y
                        new_pose.position.z = (pose_second.position.z - pose_first.position.z)/2.0 + pose_first.position.z
                        new_pose.orientation = copy.deepcopy(pose_first.orientation)
                    else :
                        new_pose = copy.deepcopy(pose_first)
                        new_pose.position.x +=0.025
                        new_pose.position.y +=0.025
                        new_pose.position.z +=0.025

                    r = range(waypoint_id,max_idx+1)
                    r.reverse()
                    for k in r:
                        old_name = str(str(ee_id) + "." + str(k) + ":" + str(self.id))
                        new_name = str(str(ee_id) + "." + str(k+1) + ":" + str(self.id))
                        self.move_waypoint(ee_id, k, k+1)

                    old_name = str(ee_id) + "." + str(max_idx) + ":" + str(self.id)
                    self.create_waypoint(ee_id, new_id, new_pose, self.parent_map[old_name], self.waypoint_controls[old_name], self.waypoint_origin[old_name], self.waypoint_pose_map[old_name])
                    self.create_from_parameters(True)

                if handle == self.menu_handles[(feedback.marker_name,"Delete Waypoint")] :
                    print "Deleting Waypoint ", waypoint_id, "for end effector: ", ee_id
                    self.remove_waypoint(ee_id, waypoint_id)
                    self.create_from_parameters(True)

                # if handle == self.menu_handles[(feedback.marker_name,"Execute On Move")] :
                #     state = self.marker_menus[feedback.marker_name].getCheckState( handle )
                #     if state == MenuHandler.CHECKED:
                #         self.marker_menus[feedback.marker_name].setCheckState( handle, MenuHandler.UNCHECKED )
                #         self.waypoint_auto_execute[ee_id] = False
                #     else :
                #         self.marker_menus[feedback.marker_name].setCheckState( handle, MenuHandler.CHECKED )
                #         self.waypoint_auto_execute[ee_id] = True
                #     rospy.loginfo(str("AffordanceTemplate::process_feedback() -- setting AutoExecute flag for ee[" + str(ee_id) + "] to " + str(self.waypoint_auto_execute[ee_id])))

                if handle == self.menu_handles[(feedback.marker_name,"Move Forward")] :
                    if waypoint_id < max_idx :
                        self.swap_waypoints(ee_id, waypoint_id, waypoint_id+1)
                        self.create_from_parameters(True)

                if handle == self.menu_handles[(feedback.marker_name,"Move Back")] :
                    if waypoint_id > 0:
                        self.swap_waypoints(ee_id, waypoint_id-1, waypoint_id)
                        self.create_from_parameters(True)

                # waypoint specific menu options
                if not feedback.marker_name in self.display_objects :

                    if handle == self.menu_handles[(feedback.marker_name,"Hide Controls")] :
                        state = self.marker_menus[feedback.marker_name].getCheckState( handle )
                        if state == MenuHandler.CHECKED:
                            self.marker_menus[feedback.marker_name].setCheckState( handle, MenuHandler.UNCHECKED )
                            self.waypoint_controls_display_on = True
                        else :
                            self.marker_menus[feedback.marker_name].setCheckState( handle, MenuHandler.CHECKED )
                            self.waypoint_controls_display_on = False

                        # this is wrong
                        self.remove_all_markers()
                        self.create_from_parameters(True)

                        rospy.loginfo(str("AffordanceTemplate::process_feedback() -- setting Hide Controls flag to " + str(self.waypoint_controls_display_on)))

                    if handle == self.menu_handles[(feedback.marker_name,"Loop Path")] :
                        state = self.marker_menus[feedback.marker_name].getCheckState( handle )
                        if state == MenuHandler.CHECKED:
                            self.marker_menus[feedback.marker_name].setCheckState( handle, MenuHandler.UNCHECKED )
                            self.waypoint_loop[ee_id] = False
                        else :
                            self.marker_menus[feedback.marker_name].setCheckState( handle, MenuHandler.CHECKED )
                            self.waypoint_loop[ee_id] = True
                        rospy.loginfo(str("AffordanceTemplate::process_feedback() -- setting Loop flag for ee[" + str(ee_id) + "] to " + str(self.waypoint_loop[ee_id])))

            self.marker_menus[feedback.marker_name].reApply( self.server )

        self.server.applyChanges()

    def stored_pose_callback(self, feedback) :
        ee_id =int(feedback.marker_name.split(".")[0])
        ee_name = self.robot_config.get_end_effector_name(ee_id)
        manipulator_name = self.robot_config.get_manipulator(ee_name)
        for p in self.robot_config.moveit_interface.get_stored_state_list(ee_name) :
            if self.menu_handles[(feedback.marker_name,"End-Effector Stored Poses",p)] == feedback.menu_entry_id :
                self.robot_config.moveit_interface.create_joint_plan_to_target(ee_name, self.robot_config.stored_poses[ee_name][p])
                r = self.robot_config.moveit_interface.execute_plan(ee_name)
                if not r : rospy.logerr(str("RobotTeleop::process_feedback(pose) -- failed moveit execution for group: " + ee_name + ". re-synching..."))
        for p in self.robot_config.moveit_interface.get_stored_state_list(manipulator_name) :
            if self.menu_handles[(feedback.marker_name,"Manipulator Stored Poses",p)] == feedback.menu_entry_id :
                self.robot_config.moveit_interface.create_joint_plan_to_target(manipulator_name, self.robot_config.stored_poses[manipulator_name][p])
                r = self.robot_config.moveit_interface.execute_plan(manipulator_name)
                if not r : rospy.logerr(str("RobotTeleop::process_feedback(pose) -- failed moveit execution for group: " + manipulator_name + ". re-synching..."))

    def create_waypoint_callback(self, feedback) :
        for ee_id in self.robot_config.end_effector_name_map.iterkeys() :
            ee_name = self.robot_config.end_effector_name_map[ee_id]
            if self.menu_handles[(feedback.marker_name,"Add Waypoint After",ee_name)] == feedback.menu_entry_id :
                print "need to add waypoint to the end of ", ee_name
                wp_id = 0
                ps = geometry_msgs.msg.Pose()
                pose_id = None
                if not ee_id in self.waypoint_max.keys() :
                    wp_name = str(str(ee_id) + "." + str(wp_id) + ":" + str(self.id))
                    T = getFrameFromPose(feedback.pose)
                    ps = getPoseFromFrame(T)
                    ps.position.x = 0.05
                    ps.position.y = 0.05
                    ps.position.z = 0.05
                    self.create_waypoint(ee_id, wp_id, ps, feedback.marker_name, pose_id)
                else :
                    wp_id = self.waypoint_max[ee_id]+1
                    wp_name = str(ee_id) + "." + str(wp_id) + ":" + str(self.id)
                    last_wp_name = str(ee_id) + "." + str(self.waypoint_max[ee_id] + ":" + str(self.id))
                    ps = getPoseFromFrame(self.objTwp[last_wp_name])
                    ps.position.x +=0.025
                    ps.position.y +=0.025
                    ps.position.z +=0.025
                    pose_id = self.waypoint_pose_map[last_wp_name]
                    self.create_waypoint(ee_id, wp_id, ps, feedback.marker_name, self.waypoint_controls[last_wp_name], self.waypoint_origin[last_wp_name], pose_id)

                self.create_from_parameters(True)

            if self.menu_handles[(feedback.marker_name,"Add Waypoint Before",ee_name)] == feedback.menu_entry_id :

                wp_id = 0
                wp_name = str(ee_id) + "." + str(0) + ":" + str(self.id)
                ps = geometry_msgs.msg.Pose()
                pose_id = None

                if not ee_id in self.waypoint_max.keys() :
                    ps = feedback.pose
                    ps.position.x = 0.05
                    ps.position.y = 0.05
                    ps.position.z = 0.05
                    self.create_waypoint(ee_id, wp_id, ps, feedback.marker_name, pose_id)
                else :
                    ps = getPoseFromFrame(self.objTwp[wp_name])
                    ps.position.x -=0.025
                    ps.position.y -=0.025
                    ps.position.z -=0.025
                    pose_id = self.waypoint_pose_map[wp_name]

                    r = range(0,self.waypoint_max[ee_id]+1)
                    r.reverse()
                    for k in r:
                        old_name = str(ee_id) + "." + str(k) + ":" + str(self.id)
                        new_name = str(ee_id) + "." + str(k+1) + ":" + str(self.id)
                        self.move_waypoint(ee_id, k, k+1)

                    self.create_waypoint(ee_id, wp_id, ps, feedback.marker_name, self.waypoint_controls[wp_name], self.waypoint_origin[wp_name], pose_id)

                self.create_from_parameters(True)

    def compute_path_ids(self, id, steps, backwards=False) :
        idx  = self.waypoint_index[id]
        max_idx = self.waypoint_max[id]
        path = []
        if steps == 0: return path, id
        cap = max_idx+1
        inc = 1
        if backwards :
            inc = -1
            if id < 0: path.append(max_idx)
        for s in range(steps) :
            idx += inc
            path.append(idx % cap)
        if backwards and id < 0: path.pop()
        return path, path[len(path)-1]

    def stop(self, end_effector) :
        manipulator_name = self.robot_config.get_manipulator(end_effector)
        self.robot_config.moveit_interface.groups[manipulator_name].stop()

    def compute_next_path_id(self, id, steps, backwards=False) :

        next_path_idx = -1
        if not id in self.waypoint_index :
            return next_path_idx

        max_idx = self.waypoint_max[id]
        if self.waypoint_index[id] < 0 :
            # haven't started yet, so set first waypoint to 0
            next_path_idx = 0
        else :
            if backwards :
                next_path_idx = self.waypoint_index[id]-steps
                if self.waypoint_loop[id] :
                    if next_path_idx < 0 :
                        next_path_idx = max_idx + next_path_idx
                else :
                    if next_path_idx < 0 :
                        next_path_idx = 0
            else :
                next_path_idx = self.waypoint_index[id]+steps
                if self.waypoint_loop[id] :
                    if  next_path_idx > max_idx :
                        next_path_idx = (self.waypoint_index[id]+steps)-max_idx
                else :
                    if  next_path_idx > max_idx :
                        next_path_idx = max_idx

        return next_path_idx

    def plan_path_to_waypoint(self, end_effector, steps=1, backwards=False, direct=False) :

        ee_id = self.robot_config.manipulator_id_map[end_effector]
        ee_offset = self.robot_config.manipulator_pose_map[end_effector]
        max_idx = self.waypoint_max[ee_id]
        manipulator_name = self.robot_config.get_manipulator(end_effector)
        ee_name = self.robot_config.get_end_effector_name(ee_id)

        # print "manipulator_name: ", manipulator_name
        # print "steps: ", steps
        # next_path_idx = self.compute_next_path_id(ee_id, steps, backwards)

        if steps == 999:
            if direct:
                path = [self.waypoint_max[ee_id]]
                next_path_idx = self.waypoint_max[ee_id]
            else :
                path, next_path_idx = self.compute_path_ids(ee_id, self.waypoint_max[ee_id] - self.waypoint_index[ee_id], backwards)
        elif steps == -999 :
            if direct:
                path = [0]
                next_path_idx = 0
            else :
                path, next_path_idx = self.compute_path_ids(ee_id, max(0,self.waypoint_index[ee_id]), backwards)
        else:
            path, next_path_idx = self.compute_path_ids(ee_id, steps, backwards)

        next_path_str = str(str(ee_id) + "." + str(next_path_idx) + ":" + str(self.id))
        # print "next_path_str: ", next_path_str
        # print "path: ", path
        rospy.loginfo(str("AffordanceTemplate::plan_path_to_waypoint() -- computing path to index[" + str(next_path_str) + "]"))

        waypoints = []
        frame_id = ""
        # print "waypoint_index: ", self.waypoint_index[ee_id]
        # print "next_path_idx: ", next_path_idx
        # print "max_idx: ", max_idx

        # if we are gonna handle pausing, it probably should allow us to interrupt things.
        for idx in path :
            next_path_str = str(str(ee_id) + "." + str(idx) + ":" + str(self.id))
            if not next_path_str in self.objTwp :
                rospy.logerr(str("AffordanceTemplate::process_feedback() -- path index[" + str(next_path_str) + "] not found!!"))
            else :
                rospy.loginfo(str("AffordanceTemplate::process_feedback() -- computing path to index[" + str(next_path_str) + "]"))
                k = str(next_path_str)
                pt = geometry_msgs.msg.PoseStamped()
                pt.header = self.server.get(k).header
                pt.pose = self.server.get(k).pose
                frame_id =  pt.header.frame_id

                T_goal = getFrameFromPose(pt.pose)
                T_offset = getFrameFromPose(ee_offset)
                T = T_goal*T_offset
                pt.pose = getPoseFromFrame(T)
                waypoints.append(pt.pose)

            if not self.waypoint_pose_map[next_path_str] == None :
                id = self.waypoint_pose_map[next_path_str]
                pn = self.robot_config.end_effector_id_map[ee_name][id]
                self.robot_config.moveit_interface.create_joint_plan_to_target(ee_name, self.robot_config.stored_poses[ee_name][pn])

        self.robot_config.moveit_interface.create_path_plan(manipulator_name, frame_id, waypoints)
        self.waypoint_plan_valid[ee_id] = True

        return next_path_idx

    def move_to_waypoint(self, end_effector, next_path_idx) :
        ee_id = self.robot_config.manipulator_id_map[end_effector]
        ee_name = self.robot_config.get_end_effector_name(ee_id)
        manipulator_name = self.robot_config.get_manipulator(end_effector)
        if self.waypoint_plan_valid[ee_id] :
            r = self.robot_config.moveit_interface.execute_plan(manipulator_name,from_stored=True)
            if not r :
                rospy.logerr(str("RobotTeleop::move_to_waypoint(mouse) -- failed moveit execution for group: " + manipulator_name + ". re-synching..."))

            r = self.robot_config.moveit_interface.execute_plan(ee_name,from_stored=True)
            if not r :
                rospy.logerr(str("RobotTeleop::process_feedback(mouse) -- failed moveit execution for group: " + ee_name + ". re-synching..."))

            self.waypoint_index[ee_id] = next_path_idx
            rospy.loginfo(str("setting current waypoint idx: " + str(self.waypoint_index[ee_id])))
            self.waypoint_plan_valid[ee_id] = False

    def create_origin_from_pose(self, ps) :
        origin = affordance_template_markers.atdf_parser.Pose()
        origin_rpy = (kdl.Rotation.Quaternion(ps.orientation.x,ps.orientation.y,ps.orientation.z,ps.orientation.w)).GetRPY()
        origin.xyz = [0]*3
        origin.rpy = [0]*3
        origin.xyz[0] = ps.position.x
        origin.xyz[1] = ps.position.y
        origin.xyz[2] = ps.position.z
        origin.rpy[0] = origin_rpy[0]
        origin.rpy[1] = origin_rpy[1]
        origin.rpy[2] = origin_rpy[2]
        return origin

    def create_6dof_controls(self, scale) :
        controls = affordance_template_markers.atdf_parser.Controls()
        controls.xyz = [0]*3
        controls.rpy = [0]*3
        controls.xyz[0] = 1
        controls.xyz[1] = 1
        controls.xyz[2] = 1
        controls.rpy[0] = 1
        controls.rpy[1] = 1
        controls.rpy[2] = 1
        controls.scale = scale
        return controls


    def terminate(self) :
        print "(EX)TERMINATE!!!!!!!!!"
        self.robot_config.tear_down()
        print "done tearing down robot config"
        rospy.sleep(2)
        self.delete_callback(None)
        self.running = False
        rospy.sleep(.5)


    def run(self) :
        while self.running :
            self.mutex.acquire()
            try :
                try :
                    for obj in self.frame_store_map.keys() :
                       self.tf_broadcaster.sendTransform((self.frame_store_map[obj].pose.position.x,self.frame_store_map[obj].pose.position.y,self.frame_store_map[obj].pose.position.z),
                                                  (self.frame_store_map[obj].pose.orientation.x,self.frame_store_map[obj].pose.orientation.y,self.frame_store_map[obj].pose.orientation.z,self.frame_store_map[obj].pose.orientation.w),
                                                  rospy.Time.now(), self.frame_store_map[obj].frame_id, self.frame_store_map[obj].root_frame_id)

                except :
                    rospy.logdebug("AffordanceTemplate::run() -- could not update thread")
            finally :
                self.mutex.release()

            rospy.sleep(.1)
        print "Killing frame update thread for AT: ", self.name
