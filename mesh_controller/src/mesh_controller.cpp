/*
 *  Copyright 2019, Sabrina Frohn
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  authors:
 *    Sabrina Frohn <sfrohn@uni-osnabrueck.de>
 *
 */

//TODO sort out unnecessary headers

#include <pluginlib/class_list_macros.h>
#include <mesh_controller/mesh_controller.h>
#include <mbf_msgs/GetPathResult.h>
#include <mesh_map/util.h>
#include <lvr2/util/Meap.hpp>
#include <lvr2/geometry/HalfEdgeMesh.hpp>
#include <tf/transform_listener.h>



#include <fstream>
#include <iostream>

PLUGINLIB_EXPORT_CLASS(mesh_controller::MeshController, mbf_mesh_core::MeshController);

namespace mesh_controller{

    MeshController::MeshController()
    {

    }

    MeshController::~MeshController()
    {

    }

    uint32_t MeshController::computeVelocityCommands(
        const geometry_msgs::PoseStamped& pose,
        const geometry_msgs::TwistStamped& velocity,
        geometry_msgs::TwistStamped &cmd_vel,
        std::string &message
        ){


        // variable that contains the planned / supposed orientation of the robot
        mesh_map::Vector plan_vec;
        if(config.useMeshGradient){
            // use supposed orientation from mesh gradient
            if(current_face){
                plan_vec = map_ptr->directionAtPosition(current_face.unwrap(), plan_vec);

            }
        } else {
            // use supposed orientation from calculated path
            plan_vec = poseToDirectionVector(current_position);
        }

        // variable to store the new angular and linear velocities
        std::vector<float> values(2);


        // used to change type of controller
        switch(config.control_type){
            case 0:
                values = naiveControl(pose, velocity, plan_vec);
                break;
            case 1:
                values = pidControl(pose, pose, velocity);
                break;
            default:
                return mbf_msgs::GetPathResult::FAILURE;
        }

        // set velocities
        cmd_vel.twist.angular.z = values.at(0);
        cmd_vel.twist.linear.x = values.at(1);
/*

        // RECORDING to measure "goodness" of travelled path
        if (record) {
            recordData(pose);
        }

        if(!config.useMeshGradient){
            // UPDATE ITERATOR to where on the plan the robot is / should be currently
            updatePlanPos(pose, values[1]);
        }
*/
        return mbf_msgs::GetPathResult::SUCCESS;
    }

    bool MeshController::isGoalReached(double dist_tolerance, double angle_tolerance)
    {
        // calculates the distance that is currently between the robot and the goal
        float current_distance = euclideanDistance(current_position, goal);

        // test if robot is within tolerable distance to goal and if the heading has a tolerable distance to goal heading
        if (current_distance <= (float)dist_tolerance && angle <= (float)angle_tolerance){
            return true;
        } else {
            return false;
        }
    }

    bool  MeshController::setPlan(const std::vector<geometry_msgs::PoseStamped> &plan)
    {
        // checks if the given vector contains the plan
        if (!plan.empty()) {
            // assign given plan to current_plan variable to make it usable for navigation
            current_plan = plan;
            return true;
        }
        else {
            return false;
        }
    }

    bool  MeshController::cancel()
    {
        return false;
    }

    float MeshController::startVelocityFactor(const geometry_msgs::PoseStamped& pose){
        float percentage;
        if(!config.useMeshGradient) {
            // calculates how far along the current plan the robot is
            percentage = (float) iter * 100 / current_plan.size();
        } else {
            // calculates how far the robot is from the goal given the distance to the goal compared to the initial distance
            // note: may not be 100% accurate due to possible curves
            percentage = euclideanDistance(pose, goal) * 100 / init_distance;
        }

        // checks if the travelled distance is within the first x percent of the total distance
        if (percentage <= config.fading){
            // calculates an increasing factor depending on how much of the x percent are travelled
            // aka closer to start: factor to set velocity closer to zero
            return percentage/config.fading;
        } else {
            return 1.0;
        }
    }

    float MeshController::endVelocityFactor(const geometry_msgs::PoseStamped& pose){
        float percentage;
        if(!config.useMeshGradient) {
            // calculates how far along the current plan the robot is
            percentage = (float) iter * 100 / current_plan.size();
        } else {
            // calculates how far the robot is from the goal given the distance to the goal compared to the initial distance
            // note: may not be 100% accurate due to possible curves
            percentage = euclideanDistance(pose, goal) * 100 / init_distance;
        }

        // checks if the travelled distance is within the last x percent of the total distance
        if (percentage >= (100-config.fading)){
            percentage = 100 - percentage;
            // calculates an decreasing factor depending on how much of the x percent are travelled
            // aka closer to goal: factor to set velocity closer to zero
            return 1 - (percentage / config.fading);
        } else {
            return 1.0;
        }
    }

    mesh_map::Vector MeshController::poseToDirectionVector(const geometry_msgs::PoseStamped &pose){
        // define tf Pose for later assignment
        tf::Stamped<tf::Pose> tfPose;
        // transform pose to tf:Pose
        poseStampedMsgToTF(pose, tfPose);

        // get x as tf:Vector of transformed pose
        tf::Vector3 v = tfPose.getBasis()[0];
        // transform tf:Vector in mesh_map Vector and return
        return mesh_map::Vector(v.x(), v.y(), v.z());
    }

    float MeshController::angleBetweenVectors(mesh_map::Vector pos, mesh_map::Vector plan){
        // calculate point product between 2 vectors to get the (smaller) angle between them
        float dot_prod = pos[0]*plan[0] + pos[1]*plan[1] + pos[2]*plan[2];
        // calculate the length of the vectors
        float length_pos = sqrt(pow(pos[0], 2) + pow(pos[1], 2) + pow(pos[2], 2));
        float length_plan = sqrt(pow(plan[0], 2) + pow(plan[1], 2) + pow(plan[2], 2));
        // return the arccos which is the smaller angle in rad (< PI)
        return acos(dot_prod / (length_pos * length_plan));
    }

    float MeshController::tanValue(float max_hight, float max_width, float value){
        // as tangens goes to pos infinity and never meets the width borders, they have to be checked individually
        if (value >= max_width/2){
            return max_hight;
        } else if (value <= -max_width/2) {
            return -max_hight;
        }
        // to widen or narrow the curve
        float width = PI/(max_width);
        // calculating corresponding y-value
        float result = max_hight * tan(width * value);
        return result;
    }

    float MeshController::gaussValue(float max_hight, float max_width, float value){
        // in case value lays outside width, the function goes to zero
        if(value > max_width){
            return 0;
        }

        //  calculating the standard deviation given the max_width
        // based on the fact that 99.7% of area lies between mü-3*sigma and mü+3*sigma
        float std_dev = pow(-max_width/6, 2);

        // calculating y value of given normal distribution
        // stretched to max_hight and desired width
        float y_value = max_hight * 1/(sqrtf(2*PI*std_dev))* pow(E, (-pow(value, 2) * pow((2*std_dev), 2)));

        return y_value;
    }

    float MeshController::direction(const mesh_map::Vector& current, const mesh_map::Vector& supposed){
        // https://www.gamedev.net/forums/topic/508445-left-or-right-direction/
        // cross product
        const mesh_map::Vector& cross_product = {current.y*supposed.z - current.z*supposed.y, current.z*supposed.x - current.x*supposed.z,
                                                 current.x*supposed.y - current.y*supposed.x};
        const mesh_map::Vector& up = {0, 0, 1};
        // dot product of result with up vector (0,0,1) will return a positive or negative result
        // positive means current is "left" of supposed and a right turn is required
        // negative means current is "right" of supposed and a left turn is required
        // zero can mean no turn or 180° turn - as we only consider the direction it will be set to right per default
        float dot_product = cross_product.x*up.x + cross_product.y*up.y + cross_product.z*up.z;

        // TODO check return values for turns
        if(dot_product < 0){
            return -1;
        } else {
            return 1;
        }
    }

    float MeshController::euclideanDistance(const geometry_msgs::PoseStamped& pose, const geometry_msgs::PoseStamped& plan_position){
        // https://en.wikipedia.org/wiki/Euclidean_distance

        // transform position of poses to position vectors
        lvr2::BaseVector<float> pose_vector = {(float)pose.pose.position.x, (float)pose.pose.position.y, (float)pose.pose.position.z};
        lvr2::BaseVector<float> plan_vector = {(float)plan_position.pose.position.x, (float)plan_position.pose.position.y, (float)plan_position.pose.position.z};

        return euclideanDistance(pose_vector, plan_vector);
    }

    float MeshController::euclideanDistance(lvr2::BaseVector<float>& current, lvr2::BaseVector<float>& planned){
        float power = 2.0;
        // calculate the euclidean distance of two position vectors
        float dist = sqrtf((pow((planned.x-current.x),power) + pow((planned.y-current.y),power) + pow((planned.z-current.z),power)));
        return dist;
    }

    void MeshController::updatePlanPos(const geometry_msgs::PoseStamped& pose, float velocity){
        // the faster the robot, the further the robot might have travelled on the planned path
        int v = static_cast<int>(velocity * 1000.0 + 1);
        // smallest[0] is iterator, smallest[1] is euclidean distance
        float smallest[2] = {static_cast<float>(iter), -1};

        for(int j = 0; j < v; j++){
            // check if end of path is reached when looking forward
            if(j > current_plan.size()){
                break;
            }
            // run over planned path from last "known"/"current" position on and calculate the distance to the current position
            float dist = euclideanDistance(pose, current_plan[(j + iter)]);
            // store the position and pose with the smallest distance
            if (dist < smallest[1] || smallest[1] == -1.0) {
                smallest[0] = j;
                smallest[1] = dist;
            }
        }

        // update iterator of path vector
        iter += static_cast<int>(smallest[0]);
        // update the current plan position to the one that has been determined as being closest to the robot now
        current_position = current_plan[iter];
    }

    std::vector<float> MeshController::lookAhead(const geometry_msgs::PoseStamped& pose, float velocity)
    {
         // select how far to look ahead depending on velocity
         int steps = (int)tanValue(1000.0, 1.0, velocity);
         // max amount of steps that will be made (in case it is interrupted earlier)
         int max_steps = steps;
         // iterator value in case drastic speed reduction has to be made
         float accum_cost = 0.0;
         float accum_turn = 0.0;

         // look ahead when using the mesh gradient for navigation reference
         if (config.useMeshGradient){
             // transforming the position of the pose to a vector
             lvr2::BaseVector<float> position_ahead = {(float)pose.pose.position.x, (float)pose.pose.position.y, (float)pose.pose.position.z};
             // assign ahead_face the face of the current position to start from
             ahead_face = current_face;
             // transforming position of goal pose to position vector
             lvr2::BaseVector<float> goal_vec = {(float)goal.pose.position.x, (float)goal.pose.position.y, (float)goal.pose.position.z};
             // steps is divided by 3 as to account for the step size of the look ahead
             for (int j = 0; j < steps/3; j++){
                 // check if the goal is reachable in next step by look ahead
                 if(euclideanDistance(position_ahead, goal_vec) < 0.04){
                     steps = j;
                     break;
                 }
                 // find the next position
                 position_ahead = step_update(position_ahead, ahead_face.unwrap());
                 // TODO find out if position ahead is direction or position vector and how to transform to the other
                 // get cost of next position
                 float new_cost = cost(position_ahead);
                 // get direction difference between next position and current position
                 float future_turn = angleBetweenVectors(poseToDirectionVector(pose), position_ahead);
                 // accumulate cost and angle
                 accum_cost += new_cost;
                 accum_turn += future_turn;
             }
         }
         // look ahead when using the planned path for navigation reference
         else {
             // adds up cost of all steps ahead
             for (int i = 0; i < steps; i++) {
                 // in case look ahead extends planned path
                 if ((iter + i) <= current_plan.size()) {
                     steps = i;
                     break;
                 }
                 geometry_msgs::PoseStamped& pose_ahead = current_plan[iter+i];
                 mesh_map::Vector pose_ahead_vec =  {(float)pose_ahead.pose.position.x, (float)pose_ahead.pose.position.y, (float)pose_ahead.pose.position.z};
                 // find cost of the future position
                 float new_cost = cost(pose_ahead_vec);
                 if (new_cost == std::numeric_limits<float>::infinity()){
                     steps = i;
                     break;
                 } else if (new_cost == -1.0){
                     // cost could not be accessed
                     // TODO check if a better solution exists
                     __throw_bad_function_call();
                 }
                 // get direction difference between current position and next position
                 float future_turn = angleBetweenVectors(poseToDirectionVector(pose), poseToDirectionVector(current_plan[iter + i]));
                 // accumulate cost and angle
                 accum_cost += new_cost;
                 accum_turn += future_turn;
             }
         }


         //  take averages
         float av_cost = accum_cost / steps;
         float av_turn = accum_turn / steps;

         // calculate the difference between the current cost and average cost
         mesh_map::Vector current_vec = poseToDirectionVector(pose);
         float current_cost = cost(current_vec);
         float cost_difference = current_cost - av_cost;
         float cost_result;
         // check if a lethal vertex is ahead
         if (cost_difference != std::numeric_limits<float>::infinity()){
             cost_result = tanValue(1.0, 2.0, cost_difference);
         } else {
             // if yes, set the linear velocity factor small depending on distance to the lethal vertex
             cost_result = gaussValue(1.0, 2*max_steps, steps);
         }

         // TODO dirction of turn for angular vel
         // calculate the difference between the current angle and the average angle
         float turn_difference = angle - av_turn;
         float turn_result = tanValue(1.0, PI, turn_difference);


         return {turn_result, cost_result};
    }

    float MeshController::cost(mesh_map::Vector& pose_vec){
        // find a face to access cost later in case none has been found yet
        if(!haveStartFace){
            current_face = map_ptr->getContainingFaceHandle(pose_vec);
            haveStartFace = true;
        }

        // call function to get cost at position through corresponding face
        float ret_cost = map_ptr->costAtPosition(current_face.unwrap(), pose_vec);
        // if ret_cost is -1 the pose is not in the current face
        if(ret_cost == -1)
        {
            // search for a neighbour face that contains the pose
            lvr2::OptionalFaceHandle new_face = searchNeighbourFaces(pose_vec, current_face.unwrap());
            if(new_face){
                // call function to get cost at position through corresponding face
                current_face = new_face;
                ret_cost = map_ptr->costAtPosition(current_face.unwrap(), pose_vec);
            } else {
                // in case no neighbour face contained the pose
                return -1.0;
            }
        }
        return ret_cost;
    }

    lvr2::OptionalFaceHandle MeshController::searchNeighbourFaces(const mesh_map::Vector& pose_vec, const lvr2::FaceHandle face){

        std::list<lvr2::FaceHandle> possible_faces;
        std::vector<lvr2::FaceHandle> neighbour_faces;
        map_ptr->mesh_ptr->getNeighboursOfFace(face, neighbour_faces);
        possible_faces.insert(possible_faces.end(), neighbour_faces.begin(), neighbour_faces.end());
        std::list<lvr2::FaceHandle>::iterator current = possible_faces.begin();

        int cnt = 0;
        int max = 40; // TODO to config


        // as long as end of list is not reached or max steps are not overstepped
        while(possible_faces.end() != current && max != cnt++) {
            lvr2::FaceHandle work_face = *current;

            float u,v;
            // check if robot position is in the current face
            if (map_ptr->barycentricCoords(pose_vec, work_face, u, v)) {
                return work_face;
            } else {
                // add neighbour of neighbour, if we overstep a small face or the peak of it
                std::vector<lvr2::FaceHandle> nn_faces;
                half_edge_mesh.getNeighboursOfFace(work_face, nn_faces);
                possible_faces.insert(possible_faces.end(), nn_faces.begin(), nn_faces.end());
            }
        }
        // in case no face that contains robot pose is found
        return lvr2::OptionalFaceHandle();
    }

    std::vector<float> MeshController::naiveControl(const geometry_msgs::PoseStamped& pose, const geometry_msgs::TwistStamped& velocity,const mesh_map::Vector plan_vec){
        mesh_map::Vector pose_vec = poseToDirectionVector(pose);
        return {-1.0, 1.0};
/*
        // ANGULAR MOVEMENT
        // calculate angle between orientation vectors
        // angle will never be negative and smaller or equal to pi
        angle = angleBetweenVectors(pose_vec, plan_vec);

        // to determine in which direction to turn (neg for left, pos for right)
        float leftRight = direction(pose_vec, plan_vec);
        // calculate a direction velocity depending on the turn direction and difference angle
        float turn_angle_diff = leftRight * tanValue(0.5, PI, angle);



        // LINEAR movement
        // basic linear velocity depending on angle difference between robot pose and plan
        float vel_given_angle = gaussValue(config.max_lin_velocity, PI, angle);
        return {turn_angle_diff, vel_given_angle};

        // ADDITIONAL factors
        // look ahead
        std::vector<float> ahead_factor = lookAhead(pose, velocity.twist.linear.x);

        // dynamic obstacle avoidance

        // slow start
        float start = startVelocityFactor(pose);
        // slower towards end
        float end = endVelocityFactor(pose);

        // adding or subtracting percentage of previously set angular velocity (turn_angle diff) depending on angular ahead factor
        // combine the basic velocity calculations with the look ahead
        float ahead_ang;
        if(ahead_factor[0] < 0){
            ahead_ang = turn_angle_diff - (1-(ahead_factor[0] / turn_angle_diff));
        } else if (ahead_factor[0] > 0){
            ahead_ang = turn_angle_diff + (1-(ahead_factor[0] / turn_angle_diff));
        } else {
            ahead_ang = turn_angle_diff;
        }

        // adding or subtracting percentage of previously set linear velocity (vel_given_angle) depending on linear ahead factor
        float ahead_lin;
        if (ahead_factor[1] < 0){
            ahead_lin = vel_given_angle - (1-(ahead_factor[1] / vel_given_angle));
        } else if (ahead_factor[1] > 0){
            ahead_lin = vel_given_angle + (1-(ahead_factor[1] / vel_given_angle));
        } else{
            ahead_lin = vel_given_angle;
        }

        // setting linear velocity depending on robot position in relation to start / goal
        float new_vel = start * end * ahead_lin;

        return {ahead_ang, new_vel};
        */
    }

    std::vector<float> MeshController::pidControl(const geometry_msgs::PoseStamped& setpoint, const geometry_msgs::PoseStamped& pv, const geometry_msgs::TwistStamped& velocity){
        // LINEAR movement
        float linear_vel = pidControlDistance(setpoint, pv);
        // ANGULAR movement
        const mesh_map::Vector& angular_sp = poseToDirectionVector(setpoint);
        const mesh_map::Vector& angular_pv = poseToDirectionVector(pv);
        float angular_vel = pidControlDir(angular_sp, angular_pv);

        // ADDITIONAL factors
        // to regulate linear velocity depending on angular velocity (higher angular vel => lower linear vel)
        float vel_given_angle = linear_vel - ((angular_vel/config.max_ang_velocity) * linear_vel);

        std::vector<float> ahead = lookAhead(pv, velocity.twist.linear.x);


        // adding or subtracting percentage of previously set angular velocity (turn_angle diff) depending on angular ahead factor
        // combine the basic velocity calculations with the look ahead
        float ahead_ang;
        if(ahead[0] < 0){
            ahead_ang = angular_vel - (1-(ahead[0] / angular_vel));
        } else if (ahead[0] > 0){
            ahead_ang = angular_vel + (1-(ahead[0] / angular_vel));
        } else {
            ahead_ang = angular_vel;
        }

        // adding or subtracting percentage of previously set linear velocity (vel_given_angle) depending on linear ahead factor
        float ahead_lin;
        if (ahead[1] < 0){
            ahead_lin = vel_given_angle - (1-(ahead[1] / vel_given_angle));
        } else if (ahead[1] > 0){
            ahead_lin = vel_given_angle + (1-(ahead[1] / vel_given_angle));
        } else{
            ahead_lin = vel_given_angle;
        }

        return {ahead_ang, ahead_lin};
    }

    float MeshController::pidControlDistance(const geometry_msgs::PoseStamped& setpoint, const geometry_msgs::PoseStamped& pv){
        // setpoint is desired position, pv is actual position
        // https://gist.github.com/bradley219/5373998

        float error = euclideanDistance(setpoint, pv);

        // proportional part
        float proportional = config.prop_dis_gain * error;

        // integral part
        int_dis_error += (error * config.int_time);
        float integral = config.int_dis_gain * int_dis_error;

        // derivative part
        float derivative = config.deriv_dis_gain * ((error - prev_dis_error) / config.int_time);

        float linear = proportional + integral + derivative;

        // TODO check if max and min output useful
        //if( output > _max )
        //    output = _max;
        //else if( output < _min )
        //    output = _min;

        prev_dis_error = error;
        return linear;
    }

    float MeshController::pidControlDir(const mesh_map::Vector& setpoint, const mesh_map::Vector& pv){
        // setpoint is desired direction, pv is actual direction
        // https://gist.github.com/bradley219/5373998

        float dir_error = angleBetweenVectors(setpoint, pv);

        // proportional part
        float proportional = config.prop_dir_gain * dir_error;

        // integral part
        int_dir_error += (dir_error * config.int_time);
        float integral = config.int_dir_gain * int_dir_error;

        // derivative part
        float derivative = config.deriv_dir_gain * ((dir_error - prev_dir_error) / config.int_time);

        float angular = proportional + integral + derivative;

        // TODO check if max and min output useful
        //if( output > _max )
        //    output = _max;
        //else if( output < _min )
        //    output = _min;

        prev_dir_error = dir_error;

        // to determine in which direction to turn (neg for left, pos for right)
        float leftRight = direction(pv, setpoint);

        return angular*leftRight;
    }

    void MeshController::recordData(const geometry_msgs::PoseStamped& robot_pose){
        float distance = euclideanDistance(robot_pose, current_position);
        string output = std::to_string(distance) + "\n";

        string info_msg = string("distance: ");

        const char* filename = "Paths.txt";
        fstream appendFileToWorkWith;

        appendFileToWorkWith.open(filename, std::fstream::in | std::fstream::out | std::fstream::app);


        // If file does not exist, Create new file
        if (!appendFileToWorkWith )
        {
            appendFileToWorkWith.open(filename,  fstream::in | fstream::out | fstream::trunc);
            appendFileToWorkWith <<info_msg;
            appendFileToWorkWith <<output;
            appendFileToWorkWith.close();

        }
        else
        {    // use existing file
            appendFileToWorkWith <<output;
            appendFileToWorkWith.close();

        }
    }

    lvr2::BaseVector<float> MeshController::step_update(mesh_map::Vector& vec, lvr2::FaceHandle face){
        // clear vector field map
        vector_map.clear();

        const auto& face_normals = map_ptr->faceNormals();

        bool foundConnectedFace = false;
        std::list<lvr2::FaceHandle> possible_faces;
        std::vector<lvr2::FaceHandle> neighbour_faces;
        map_ptr->mesh_ptr->getNeighboursOfFace(face, neighbour_faces);
        possible_faces.insert(possible_faces.end(), neighbour_faces.begin(), neighbour_faces.end());
        std::list<lvr2::FaceHandle>::iterator current = possible_faces.begin();
        mesh_map::Vector dir;
        float step_width = 0.03;

        // Set start distance to zero
        // add start vertex to priority queue
        for(auto vH : map_ptr->mesh_ptr->getVerticesOfFace(face))
        {
            const mesh_map::Vector diff = vec - map_ptr->mesh_ptr->getVertexPosition(vH);
            vector_map.insert(vH, diff);
        }

        int cnt = 0;
        int max = 40; // TODO to config

        while(possible_faces.end() != current && max != cnt++)
        {
            lvr2::FaceHandle fH = *current;
            auto vertices = map_ptr->mesh_ptr->getVertexPositionsOfFace(fH);
            auto face_vertices = map_ptr->mesh_ptr->getVerticesOfFace(fH);
            float u, v;

            // Projection onto the triangle plane
            mesh_map::Vector tmp_vec = mesh_map::projectVectorOntoPlane(vec, vertices[0], face_normals[fH]);

            // Check if the projected point lies in the current testing face
            if(vector_map.containsKey(face_vertices[0]) && vector_map.containsKey(face_vertices[1]) && vector_map.containsKey(face_vertices[2])
               && mesh_map::barycentricCoords(tmp_vec, vertices[0], vertices[1], vertices[2], u, v))
            {
                foundConnectedFace = true;
                // update ahead_face as face of the new vector
                ahead_face = fH;
                vec = tmp_vec;
                float w = 1 - u - v;
                dir = ( vector_map[face_vertices[0]]*u + vector_map[face_vertices[1]]*v + vector_map[face_vertices[2]]*w ).normalized() * step_width ;
                break;
            }
            else
            {
                // add neighbour of neighbour, if we overstep a small face or the peak of it
                std::vector<lvr2::FaceHandle> nn_faces;
                map_ptr->mesh_ptr->getNeighboursOfFace(fH, nn_faces);
                possible_faces.insert(possible_faces.end(), nn_faces.begin(), nn_faces.end());
            }
            current++;
        }

        if(!foundConnectedFace){
            return lvr2::BaseVector<float>();
        }

        return vec + dir;
    }

    /*
    void MeshController::obstacleAvoidance(pose, mesh_map, path){
        // make new measurement + make mesh layer of it
        // compare new measurement to old one - find new lethal faces
        // check if new lethal faces are on path
        // check if new lethal faces are on path
            // if on path: reduce velocity + make new plan

    }
    */

    void MeshController::reconfigureCallback(mesh_controller::MeshControllerConfig& cfg, uint32_t level)
    {

        ROS_INFO("Reconfigure Request: %f %f %f %f %f %f %s %f %f %f %f %i ",
                config.prop_dis_gain,
                config.int_dis_gain,
                config.deriv_dis_gain,
                config.prop_dir_gain,
                config.int_dir_gain,
                config.deriv_dir_gain,
                config.useMeshGradient?"True":"False",
                config.max_lin_velocity,
                config.max_ang_velocity,
                config.fading,
                config.int_time,
                config.control_type);

        if (first_config)
        {
            config = cfg;
            first_config = false;
        }

        config = cfg;
    }

    bool  MeshController::initialize(
            const std::string& plugin_name,
            const boost::shared_ptr<tf2_ros::Buffer>& tf_ptr,
            const boost::shared_ptr<mesh_map::MeshMap>& mesh_map_ptr){

        name = plugin_name;
        private_nh = ros::NodeHandle("~/"+name);

        ROS_INFO_STREAM("Namespace of the controller: " << private_nh.getNamespace());
        // all for mesh plan
        map_ptr = mesh_map_ptr;

        // iterator to go through plan vector
        iter = 0;

        // for PID
        // initialize integral error for PID
        int_dis_error = 0.0;
        int_dir_error = 0.0;

        haveStartFace = false;

        // for recording - true = record, false = no record
        record = false;

        reconfigure_server_ptr = boost::shared_ptr<dynamic_reconfigure::Server<mesh_controller::MeshControllerConfig> > (
                new dynamic_reconfigure::Server<mesh_controller::MeshControllerConfig>(private_nh));

        config_callback = boost::bind(&MeshController::reconfigureCallback, this, _1, _2);
        reconfigure_server_ptr->setCallback(config_callback);

        // TODO That does not make any sense here in the init
        if(!current_plan.empty()){
            goal = current_plan.back();

            // TODO ist immer 0?!
            // initial distance between goal and robot position
            init_distance = euclideanDistance(current_plan.front(), goal);

        } else {
            return false;
        }

        return true;

    }
} /* namespace mesh_controller */
