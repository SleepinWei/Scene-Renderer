#include"PTVec.h"
//extern Camera camera;
#include<cmath>

PT::vec3::vec3(double x, double y, double z):x(x),y(y),z(z) {
}
PT::vec3::vec3(double x):x(x),y(x),z(x) {
}
PT::vec3 PT::vec3::operator*(double t) {
	return vec3(x * t, y * t, z * t);
}
PT::vec3 PT::vec3::operator+(double t) {
	return vec3(x + t, y + t, z + t);
}
PT::vec3 PT::vec3::operator-(double t) {
	return vec3(x - t, y - t, z - t);
}
PT::vec3 PT::vec3::operator/(double t) {
	return vec3(x / t, y / t, z / t);
}
PT::vec3 PT::operator* (double t, PT::vec3 v) {
	return v.operator*(t);
}
PT::vec3 PT::operator+ (double t, PT::vec3 v) {
	return v.operator+(t);
}
PT::vec3 PT::operator- (double t, PT::vec3 v) {
	return PT::vec3(t - v.x, t - v.y, t - v.z);
}

double PT::dot(vec3 a, vec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

double PT::length(vec3 v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
PT::vec3 PT::normalize(PT::vec3 v) {
	return v / length(v);
}

PT::vec3 PT::vec3::operator+(vec3 v) {
	return vec3(x + v.x, y + v.y, z + v.z);
}

PT::vec3 PT::vec3::operator-(vec3 v) {
	return vec3(x - v.x, y - v.y, z - v.z);
}
PT::vec3 PT::vec3::operator-(void) {
	return vec3(-x, -y, -z);
}

PT::vec3 PT::vec3::random() {
	return vec3(random_double(), random_double(), random_double());
}

PT::vec3 PT::vec3::random(double min, double max) {
	return vec3(random_double(min, max), random_double(min, max), random_double(min, max));
}

PT::vec3 PT::random_direction() {
	double theta = random_double(0, PI / 2);
	double phi = random_double(0, 2 * PI);
	return vec3(sin(theta) * sin(phi), cos(theta), sin(theta) * cos(phi));
	// y axis is UP, z axis points out of the screen, x axis points right 
}
PT::vec3 PT::cross(vec3 a, vec3 b) {
	return vec3(
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	);
}
PT::vec3 PT::reflect(vec3& v, vec3& n) {
	return v - 2 * dot(v, n) * n;
}

PT::vec3 PT::vec3::operator*(vec3& v) {
	return vec3(x * v.x, y * v.y, z * v.z);
}
PT::vec3 PT::vec3::operator/ (vec3& v) {
	return vec3(x / v.x, y / v.y, z / v.z);
}

