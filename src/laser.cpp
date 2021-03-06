#include "ros/ros.h"
#include "sensor_msgs/LaserScan.h"
#include <iostream>
#include <vector>
#include <math.h>
#include <mine_detection/Obstacle.h>
#include <visualization_msgs/Marker.h>

//#include "obstacle.h"

//define twodimensional point as struct. 
struct Point
{
    double x;
    double y;
};

std::vector<Point> pointsi;

ros::Subscriber laser_sub;
ros::Publisher obstacle_pub;
ros::Publisher rviz_pub;

void laserCallback(const sensor_msgs::LaserScan::ConstPtr &laser_msg)
{
    float angle;
    int count = 0;
    bool isInRangeRight;
    bool isInRangeLeft;
    double range = 1.5;

    points.clear();
    points.shrink_to_fit();
    //std::cout << "New array:" << std::endl;
    for (int i = 0; i < laser_msg->ranges.size(); i++)
    {
        angle = laser_msg->angle_min + (i * laser_msg->angle_increment);
        //std::cout << angle << std::endl;
        if (!std::isnan(laser_msg->ranges.at(i)))
        {
            //std::cout << "Callback" << std::endl;
            if (laser_msg->range_min < laser_msg->ranges[i] && laser_msg->ranges[i] < range)
            {
                //feature to make sure the obstacle is completely in range, however it is not used.
                //isInRangeRight = (laser_msg->range_min < laser_msg->ranges[0] && laser_msg->ranges[0] < range);
                //isInRangeLeft = (laser_msg->range_min < laser_msg->ranges[laser_msg->ranges.size() - 1] && laser_msg->ranges[laser_msg->ranges.size() - 1] < range);
                //if (!isInRangeRight && !isInRangeLeft)
                //{
                //std::cout << laser_msg->ranges.at(i) << std::endl;
                Point p;
                //calculate the cartesian coordinates.
                p.x = laser_msg->ranges[i] * cos(angle);
                p.y = laser_msg->ranges[i] * sin(angle);
                //std::cout << p.x << " : " << p.y << std::endl;
                points.push_back(p);
                //}
            }
        }
    }
}

Point getCenterOfCircle(std::vector<Point> *points)
{
    //get first, middle and last point of the ranges array which is on the obstacle.
    Point f = (*points).at(0);
    Point m = (*points).at(points->size() / 2);
    Point l = (*points).at(points->size() - 1);

    //calculate determinants
    double A = f.x * (m.y - l.y) - f.y * (m.x - l.x) + m.x * l.y - l.x * m.y;
    double B = (pow(f.x, 2) + pow(f.y, 2)) * (l.y - m.y) + (pow(m.x, 2) + pow(m.y, 2)) * (f.y - l.y) + (pow(l.x, 2) + pow(l.y, 2)) * (m.y - f.y);
    double C = (pow(f.x, 2) + pow(f.y, 2)) * (m.x - l.x) + (pow(m.x, 2) + pow(m.y, 2)) * (l.x - f.x) + (pow(l.x, 2) + pow(l.y, 2)) * (f.x - m.x);

    //Create center point.
    Point center;
    center.x = -B / (2 * A);
    center.y = -C / (2 * A);

    //return
    return center;
}

//calculate radius from center to peripheral point.
double obstacleRadius(Point center, Point per_coordinate)
{
    return sqrt(pow(center.x - per_coordinate.x, 2) + pow(center.y - per_coordinate.y, 2));
}

int main(int argc, char *argv[])
{
    //init laser_scan node
    ros::init(argc, argv, "laser_scan");
    ros::NodeHandle n;

    //assign ros semantics.
    laser_sub = n.subscribe<sensor_msgs::LaserScan>("/scan", 10, &laserCallback);
    //use custom obstacle message type.
    obstacle_pub = n.advertise<mine_detection::Obstacle>("/obstacle", 10);
    ros::Rate loop_rate(10);

    //initialize center point and radius of obstacle
    Point center;
    double radius;
    //initialize new obstacle message.
    mine_detection::Obstacle obstacle_msg;
    while (ros::ok())
    {
        ros::spinOnce();
        if (points.size() > 3)
        {
            //get center of obstacle.
            center = getCenterOfCircle(&points);

            //assign message point to the obstacle center.
            obstacle_msg.x = center.x;
            obstacle_msg.y = center.y;
            
            //get radius of obstacle and assign it to the message.
            radius = obstacleRadius(center, points[0]);
            obstacle_msg.r = radius;

            //publish only if the obstacle is reasonable size.
            if (radius < 0.35)
                {
                    obstacle_pub.publish(obstacle_msg);
                }
        }
        loop_rate.sleep();
    }
    return 0;
}
