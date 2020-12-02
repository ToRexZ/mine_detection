#include "ros/ros.h"
#include "sensor_msgs/LaserScan.h"
#include <iostream>
#include <vector>
#include <math.h>
#include "mine_detection/Obstacle.h"

//#include "obstacle.h"

struct Point
{
    double x;
    double y;
};

std::vector<Point> points;
Point laser_offset = {0.08, 0.025};

ros::Subscriber laser_sub;
ros::Publisher obstacle_pub;

void laserCallback(const sensor_msgs::LaserScan::ConstPtr &laser_msg)
{
    float angle;
    int count = 0;
    points.clear();
    points.shrink_to_fit();
    //std::cout << "New array:" << std::endl;
    for (int i = 0; i < laser_msg->ranges.size(); i++)
    {
        angle = laser_msg->angle_min + (i * laser_msg->angle_increment);
        if (!std::isnan(laser_msg->ranges.at(i)))
        {
            if (0.5 < laser_msg->ranges[i] && laser_msg->ranges[i] < 1)
            {
                //std::cout << laser_msg->ranges.at(i) << std::endl;
                Point p;
                //calculate the cartesian coordinates.
                p.x = laser_msg->ranges[i] * cos(angle);
                p.y = laser_msg->ranges[i] * sin(angle);
                //std::cout << p.x << " : " << p.y << std::endl;
                points.push_back(p);
            }
        }
    }
    //std::cout << "Point count is: " << points.size() << std::endl;
}

Point getCenterOfCircle(std::vector<Point> *points)
{
    Point f = (*points).at(0);
    Point m = (*points).at(points->size() / 2);
    Point l = (*points).at(points->size() - 1);

    // std::cout << f.x << " : " << f.y << std::endl;
    // std::cout << m.x << " : " << m.y << std::endl;
    // std::cout << l.x << " : " << l.y << std::endl;

    //calculate determinants
    double A = f.x * (m.y - l.y) - f.y * (m.x - l.x) + m.x * l.y - l.x * m.y;
    double B = (pow(f.x, 2) + pow(f.y, 2)) * (l.y - m.y) + (pow(m.x, 2) + pow(m.y, 2)) * (f.y - l.y) + (pow(l.x, 2) + pow(l.y, 2)) * (m.y - f.y);
    double C = (pow(f.x, 2) + pow(f.y, 2)) * (m.x - l.x) + (pow(m.x, 2) + pow(m.y, 2)) * (l.x - f.x) + (pow(l.x, 2) + pow(l.y, 2)) * (f.x - m.x);

    // std::cout << "A : " << A << std::endl;
    // std::cout << "B : " << B << std::endl;
    // std::cout << "C : " << C << std::endl;

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
    ros::init(argc, argv, "laser_test");
    ros::NodeHandle n;

    laser_sub = n.subscribe<sensor_msgs::LaserScan>("/scan", 10, &laserCallback);
    obstacle_pub = n.advertise<mine_detection::Obstacle>("/obstacle", 10);

    ros::Rate loop_rate(10);
    Point center;
    mine_detection::Obstacle obstacle_msg;
    while (ros::ok())
    {
        ros::spinOnce();
        if (points.size() > 3)
        {
            center = getCenterOfCircle(&points);

            obstacle_msg.x = center.x;
            obstacle_msg.y = center.y;
            obstacle_msg.r = obstacleRadius(center, points[0]);

            obstacle_pub.publish(obstacle_msg);
        }
        loop_rate.sleep();
    }
    return 0;
}
