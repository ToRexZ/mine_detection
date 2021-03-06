//include packages

#include "ros/ros.h"
#include "geometry_msgs/Twist.h"
#include "std_msgs/Float32.h"
#include "mine_detection/Obstacle.h"

#include <math.h>
#include <iostream>
#include <turtlesim/Pose.h>
#include <nav_msgs/Odometry.h>
#include <std_msgs/Empty.h>
#include <visualization_msgs/Marker.h>
#include <move.h>
#include <points_gen.h>

//include namespaces.
using namespace std;
using namespace N;
using namespace Points_gen;

//Initialize ros semantics.
ros::Publisher reset_pub;
ros::Publisher vel_pub;
ros::Subscriber sub_pose;
ros::Publisher points_pub;

ros::Subscriber obstacle_sub;

ros::Publisher *pointPtr;

//create a vector2D struct
struct Vector2D
{
    double x;
    double y;
};

//prototypes
bool IsClockwise(double angleActual, double angleDesired);
bool VectorInUpperQuadrants(Vector2D vector);
Vector2D rotateVectorByAngle(double angle, Vector2D vector);
Vector2D vectorByAngle(double angle);
double getTheta(double angle);
void rotate(Point goal);
void poseCallback(const nav_msgs::Odometry::ConstPtr &pose_message);
visualization_msgs::Marker getRvizObstacle(const Vector2D *center, double radius);
double euclidean_distance(double x1, double y1, double x2, double y2);
double linear_velocity(Point goal);
double angular_velocity(Point goal);
double getAngle(Point goal);
void move2goal(Point goal, Point stop_goal);

//point distance tolerance.
const double distance_tolerance = 0.10;

//laser offset
Vector2D offset = {0.08, 0.025};
//obstacle position in odometry coordinate system.
Vector2D obstacle_odom;
//obstacle radius
double radius;
double contour_offset = 0.25; //robot offset in meters
double robot_radius = 0.175;  //robot radius in meters

//current turtlebot pose using the turtlesim object type.
turtlesim::Pose cur_pose;

#pragma region Quaternion To Euler Angles conversion
struct Quaternion
{
    double x, y, z, w;
};

struct EulerAngles
{
    double roll, pitch, yaw;
};

EulerAngles angles;

//convert quarternion into eulerangles.
EulerAngles ToEulerAngles(Quaternion q)
{
    EulerAngles angles;

    //roll (x axis rotation)
    double sinr_cosp = 2 * (q.w * q.x - q.y * q.z);
    double cosr_cosp = 1 - 2 * (q.x * q.x + q.y * q.y);
    angles.roll = atan2(sinr_cosp, cosr_cosp);

    //pitch (y axis rotation)
    double sinp = 2 * (q.w * q.y - q.z * q.x);
    if (abs(sinp) >= 1)
        //copysign returns the magnitude of M_PI/2 with the sign of sinp
        angles.pitch = copysign(M_PI / 2, sinp); // use 90 degrees if out of range
    else
        angles.pitch = asin(sinp);

    // yaw (z-axis rotation)
    double siny_cosp = 2 * (q.w * q.z + q.x * q.y);
    double cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
    //calculate yaw, and make the yaw angle be 0 < yaw < 2pi.
    angles.yaw = std::atan2(siny_cosp, cosy_cosp);

    return angles;
}
#pragma endregion

//Callback function when a odometry message is recieved.
void poseCallback(const nav_msgs::Odometry::ConstPtr &pose_message)
{
    // Get the x,y position.
    cur_pose.x = pose_message->pose.pose.position.x;
    cur_pose.y = pose_message->pose.pose.position.y;

    // Quaternion object q.
    Quaternion q;

    // Assign values of pose message to quaternion.
    q.x = pose_message->pose.pose.orientation.x;
    q.y = pose_message->pose.pose.orientation.y;
    q.z = pose_message->pose.pose.orientation.z;
    q.w = pose_message->pose.pose.orientation.w;

    // Retrieve Euler angles from quaternion pose message.
    angles = ToEulerAngles(q);
    
    //assign the yaw angle to current orientation.
    cur_pose.theta = angles.yaw;

    //std::cout << "angle: " << angles.yaw << " x: " << cur_pose.x << " y: " << cur_pose.y << std::endl;
}

//Callback function when an obstacle message is recieved.
void obstacleCallback(const mine_detection::Obstacle::ConstPtr &obs_msg)
{
    //Obstacle position compared to the robot base.
    Vector2D obstacle_robot;
    obstacle_robot.x = obs_msg->x - offset.x;
    obstacle_robot.y = obs_msg->y - offset.y;
    
    //rotate obstacle center around the robot center, to match the odometry orientation.
    Vector2D obstacle_robot_rotated = rotateVectorByAngle(getTheta(cur_pose.theta), obstacle_robot);
    
    //assign the obstacle to the obstacle odom object.
    obstacle_odom.x = cur_pose.x + obstacle_robot_rotated.x;
    obstacle_odom.y = cur_pose.y + obstacle_robot_rotated.y;
    radius = obs_msg->r;
    
    //call rviz publish pointer to publish the returned rviz obstacle from getRvizObstacle().
    pointPtr->publish(getRvizObstacle(&obstacle_odom, obs_msg->r));
}

//get cylinder to publish in rviz.
visualization_msgs::Marker getRvizObstacle(const Vector2D *center, double radius)
{
    //define a visualization marker with the obstacle center and a radius, and make it white.
    visualization_msgs::Marker points;
    points.header.frame_id = "/odom";
    points.ns = "obstacle namespace";
    points.action = visualization_msgs::Marker::ADD;

    points.pose.orientation.w = 1.0;
    points.header.stamp = ros::Time::now();

    points.id = 0;

    points.type = visualization_msgs::Marker::CYLINDER;

    points.scale.x = radius * 2;
    points.scale.y = radius * 2;
    points.scale.z = 0.5;

    points.color.r = 1.0f;
    points.color.g = 1.0f;
    points.color.b = 1.0f;
    points.color.a = 1.0;

    geometry_msgs::Point point;
    point.x = (*center).x;
    point.y = (*center).y;

    points.pose.position.x = (*center).x;
    points.pose.position.y = (*center).y;
    points.points.push_back(point);

    return points;
}

//Return new path point which is offset to the edge of the obstacle.
Point offsetPointInObstacle(Point path_point, double r, Vector2D obstacle)
{
    //get angle to offset the point to the edge.
    double angle = asin((obstacle.y - path_point.y) / r); 
    
    //if in the left side of the obstacle, offset points to the left.
    //this ensures the shortest path around the obstacle.
    if (path_point.x > obstacle.x)
    {
        
        path_point.x = obstacle.x + r * cos(angle);
        return path_point;
    }
    else
    {
        path_point.x = obstacle.x + r * (-cos(angle));
        return path_point;
    }
}
//set the path radius to be around the obstacle, with a contour offset.
double path_radius = radius + robot_radius + contour_offset;

//if the point is in obstacle.
bool isInObstacle(Point p)
{
    return euclidean_distance(p.x, p.y, obstacle_odom.x, obstacle_odom.y) < path_radius;
}

int main(int argc, char *argv[])
{
    //init new node called mine_detection_path_planning
    ros::init(argc, argv, "mine_detection_path_planning");
    ros::NodeHandle n;

    //assign semantics to the right topics and with the right queue sizes. 
    points_pub = n.advertise<visualization_msgs::Marker>("/visualization_marker", 200);
    reset_pub = n.advertise<std_msgs::Empty>("/mobile_base/commands/reset_odometry", 10);
    vel_pub = n.advertise<geometry_msgs::Twist>("/cmd_vel_mux/input/navi", 10);
    sub_pose = n.subscribe("/odom", 1000, &poseCallback);
    obstacle_sub = n.subscribe("/obstacle", 10, &obstacleCallback);

    //assign the reference of points_pub to pointPtr.
    pointPtr = &points_pub;

    //wait untill the mobile base is connected to reset odometry. 
    ROS_INFO("Resetting odometry...");
    while (reset_pub.getNumSubscribers() == 0)
    {
        ros::spinOnce();
    }

    //publish empty string to reset odometry.
    std_msgs::Empty e;
    reset_pub.publish(e);
    ROS_INFO("Reset succesfully");

    ros::Rate loop_rate(10);

    Point goal_pose;

    //create instance of points_list.
    points_List points_instance;

    //create a vector of points.
    std::vector<Points_gen::Point> vec;

    //retrieve points from pointsgen.cpp file.
    vec = points_instance.gen_Point_list();

    //check if points pub has subscribers.

    ros::Rate retry_rate(1);
    for (int i = 0; i < 10; i++)
    {
        if (points_pub.getNumSubscribers() != 0)
        {
            //publish points to rviz.
            points_instance.rvizPoints(points_pub, vec);
            ROS_INFO("Connected to rviz.");
            break;
        }
        ROS_WARN("Connecting to rviz...");
        if (i == 9)
        {
            ROS_ERROR("Could not connect to rviz.");
            break;
        }
        retry_rate.sleep();
    }

    std::cin.get();
    //process callback to ensure connections are established.
    ros::spinOnce();

    double percentage = 0;

    //check if vel_pub has subscribers.
    if (vel_pub.getNumSubscribers() != 0)
    {
        //loop through the vector.
        for (int i = 0; i < vec.size(); i++)
        {
            ros::spinOnce();
            percentage = i;
            std::cout << std::fixed << std::setprecision(2) << percentage / 195 * 100 << "% cleared." << std::endl;

            //create a point from each element.
            Point p = vec[i];

            //temp is used to count until a stop points is reached.
            int temp = i;

            //if at stop: rotate.
            if (vec[temp].stop || vec[temp - 1].stop)
            {
                rotate(p);
            }
            //initialize to counters to start at the current index.
            int count = i;
            int count2 = i;
            
            //as long as there is no stop point...
            while (!vec[count2].stop)
            {
                //check if the point is in obstacle.
                if (isInObstacle(vec[count2]))
                {
                    //as long as it is not in obstacle, and not a stop point.
                    while (!isInObstacle(vec[count]) && !vec[count].stop)
                    {
                        //increment first counter 
                        count++;
                    };
                    //when in obstacle, set stop point before obstacle.
                    if (isInObstacle(vec[count]))
                    {
                        vec[count - 1].stop = true;
                    }
                    //while in obstacle, and not a stop point. 
                    while (isInObstacle(vec[count]) && !vec[count].stop)
                    {
                        //offset points by the path radius.
                        vec[count] = offsetPointInObstacle(vec[count], path_radius, obstacle_odom);
                        //increment first counter
                        count++;
                    };
                    //set stop point after obstacle.
                    if (isInObstacle(vec[count - 1]))
                    {
                        vec[count].stop = true;
                    }
                    //publish new path points to rviz.
                    points_instance.rvizPoints(points_pub, vec);
                }
                //increment other counter
                count2++;
            }
            //count untill the next stop point.
            while (!vec[temp].stop)
            {
                temp++;
            }
            //move to the goal, using the next point p, as angular vel,
            //and temp, as the linear vel guide.
            move2goal(p, vec[temp]);
        }
        std::cout << "Done";
    }
    //else throw connection error.
    else
    {
        ROS_ERROR("Could not connect to turtlebot...");
    }

    return 0;
}

/**
 * makes the robot move forward with a certain linear velocity for a
 * certain distance in a forward or backward straight direction.
 * */

double getTheta(double angle)
{
    //If theta is negative it is converted to the corresponding positive angle (Theta becomes negative when the turtle rotates clockwise).
    double theta = angle < 0 ? angle + 2 * M_PI : angle;
    //cout << "Got theta: " << theta << endl;
    return theta;
}

void rotate(Point goal)
{
    geometry_msgs::Twist vel_msg;

    double desired_angle = getTheta(getAngle(goal));

    // Sets all the velocities equal to zero, except angular.z.
    vel_msg.linear.x = 0;
    vel_msg.linear.y = 0;
    vel_msg.linear.z = 0;

    vel_msg.angular.x = 0;
    vel_msg.angular.y = 0;

    ros::spinOnce();

    // Rotates either clockwise (if=true) or counterclockwise (if=false) depending on which is shortest.

    ros::Rate loop_rate(1000);
    // Rotates until turtle has rotated to desired angle (within 0.02 radians).
    do
    {
        if (IsClockwise(getTheta(cur_pose.theta), desired_angle))
        {
            vel_msg.angular.z = -fabs(angular_velocity(goal));
        }
        else
        {
            vel_msg.angular.z = fabs(angular_velocity(goal));
        }
        //publish velocity
        vel_pub.publish(vel_msg);
        ros::spinOnce();
        loop_rate.sleep();
    } while (fabs(desired_angle - getTheta(cur_pose.theta)) > 0.05 && ros::ok());

    // Stops the turtle from rotating.
    vel_msg.angular.z = 0;
    vel_pub.publish(vel_msg);
}

#pragma region Shortest rotation
// The function determines if the shortest rotation between the actual angle and the desired angle is clockwise or counterclockwise.
bool IsClockwise(double angleActual, double angleDesired)
{
    Vector2D vectorActual = vectorByAngle(angleActual);
    Vector2D vectorDesired = vectorByAngle(angleDesired);

    Vector2D rotatedVectorActual = rotateVectorByAngle(2 * M_PI - angleActual, vectorActual);
    Vector2D rotatedVectorDesired = rotateVectorByAngle(2 * M_PI - angleActual, vectorDesired);

    return !VectorInUpperQuadrants(rotatedVectorDesired);
}

// The function determines whether the rotated desired vector is in the upper quadrants (above the x-axis) or not.
bool VectorInUpperQuadrants(Vector2D vector)
{
    // If the vectors y-value is greater than zero it means that the vector is in the upper quadrants.
    return vector.y >= 0;
}

// The function rotates a vector around Origo and the x-axis by a given angle.
Vector2D rotateVectorByAngle(double angle, Vector2D vector)
{
    Vector2D rotatedVector;
    rotatedVector.x = vector.x * cos(angle) + vector.y * (-sin(angle));
    rotatedVector.y = vector.x * sin(angle) + vector.y * cos(angle);
    return rotatedVector;
}

// The function takes in an angle and makes it into a unit vector.
Vector2D vectorByAngle(double angle)
{
    Vector2D vector;
    vector.x = cos(angle);
    vector.y = sin(angle);
    return vector;
}
#pragma endregion

//calculate euclidean distance between two points.
double euclidean_distance(double x1, double y1, double x2, double y2)
{
    return sqrt(pow((x2 - x1), 2) + pow((y2 - y1), 2));
}

//calculate linear velocity using euclidean distance and having a max linear velocity.
double linear_velocity(Point goal)
{
    double kv = 0.5;
    double max_linear_vel = 0.2;

    double linear_vel = kv * euclidean_distance(cur_pose.x, cur_pose.y, goal.x, goal.y);

    return fabs(linear_vel) > max_linear_vel ? copysign(max_linear_vel, linear_vel) : linear_vel;
}

//calculate angular velocity based on angular difference between the robot orienation and the a goal point.
double angular_velocity(Point goal)
{
    double ka = 2.5;
    double max_angular_vel = 1.0;

    double angular_vel = ka * (getTheta(getAngle(goal)) - getTheta(cur_pose.theta));

    return fabs(angular_vel) > max_angular_vel ? copysign(max_angular_vel, angular_vel) : angular_vel;
}

// The function determines in which direction (meaning at what angle) it should move to get from the current position to the goal position.
double getAngle(Point goal)
{
    return atan2(goal.y - cur_pose.y, goal.x - cur_pose.x);
}

// The function makes the turtle move to the given goal.
void move2goal(Point goal, Point stop_goal)
{

    geometry_msgs::Twist vel_msg;
    ros::Rate loop_rate = (100);

    while (euclidean_distance(cur_pose.x, cur_pose.y, goal.x, goal.y) > distance_tolerance && ros::ok())
    {
        // std::cout << "x: " << cur_pose.x << std::endl << "y: " << cur_pose.y << std::endl << "theta: " << cur_pose.theta << std::endl;

        // Sets the linear velocity in the direction of the x-axis to a decreasing speed (look at linear_velocity function) depending on where the goal is.
        vel_msg.linear.x = linear_velocity(stop_goal);
        vel_msg.linear.y = 0;
        vel_msg.linear.z = 0;

        // Sets the angular veloity around the z-axis to a speed (look at angular_velocity function) depending on the where the goal is.
        vel_msg.angular.x = 0;
        vel_msg.angular.y = 0;
        vel_msg.angular.z = angular_velocity(goal);

        vel_pub.publish(vel_msg);

        loop_rate.sleep();
        ros::spinOnce();
    }

    // Sets the velocity (in all directions and rotations) to zero.
    vel_msg.linear.x = 0;
    vel_msg.angular.z = 0;
    vel_pub.publish(vel_msg);
}
