// #include"PT/PTVec.h"
// //extern PTCamera camera;
// #include<cmath>

// vec3::vec3(double x, double y, double z):x(x),y(y),z(z) {
// }
// vec3::vec3(double x):x(x),y(x),z(x) {
// }
// vec3 vec3::operator*(float t)const {
// 	return vec3(x * t, y * t, z * t);
// }
// vec3 vec3::operator+(float t)const {
// 	return vec3(x + t, y + t, z + t);
// }
// vec3 vec3::operator-(float t)const {
// 	return vec3(x - t, y - t, z - t);
// }
// vec3 vec3::operator/(float t)const {
// 	return vec3(x / t, y / t, z / t);
// }
// vec3 operator* (double t, const vec3& v) {
// 	return v.operator*(t);
// }
// vec3 operator+ (double t, const vec3& v) {
// 	return v.operator+(t);
// }
// vec3 operator- (double t, const vec3& v) {
// 	return vec3(t - v.x, t - v.y, t - v.z);
// }

// double dot(const vec3& a, const vec3& b) {
// 	return a.x * b.x + a.y * b.y + a.z * b.z;
// }

// double length(const vec3& v) {
// 	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
// }
// vec3 normalize(const vec3& v) {
// 	return v / length(v);
// }

// vec3 vec3::operator+(const vec3& v)const {
// 	return vec3(x + v.x, y + v.y, z + v.z);
// }

// vec3 vec3::operator-(const vec3& v)const {
// 	return vec3(x - v.x, y - v.y, z - v.z);
// }
// vec3 vec3::operator-(void)const {
// 	return vec3(-x, -y, -z);
// }

// vec3 vec3::random() {
// 	return vec3(random_double(), random_double(), random_double());
// }

// vec3 vec3::random(double min, double max) {
// 	return vec3(random_double(min, max), random_double(min, max), random_double(min, max));
// }

// vec3 random_direction() {
// 	double theta = random_double(0, PI / 2);
// 	double phi = random_double(0, 2 * PI);
// 	return vec3(sin(theta) * sin(phi), cos(theta), sin(theta) * cos(phi));
// 	// y axis is UP, z axis points out of the screen, x axis points right 
// }
// vec3 cross(const vec3& a, const vec3& b) {
// 	return vec3(
// 		a.y * b.z - a.z * b.y,
// 		a.z * b.x - a.x * b.z,
// 		a.x * b.y - a.y * b.x
// 	);
// }
// vec3 reflect(const vec3& v, const vec3& n){
// 	return v - 2 * dot(v, n) * n;
// }

// vec3 vec3::operator*(const vec3& v) const{
// 	return vec3(x * v.x, y * v.y, z * v.z);
// }
// vec3 vec3::operator/ (const vec3& v) const{
// 	return vec3(x / v.x, y / v.y, z / v.z);
// }

// vec3 random_in_hemisphere(const vec3& normal) {
// 		vec3 random_dir = random_in_unit_sphere();
// 		if (dot(random_dir, normal) > 0.0) {
// 			return random_dir;
// 		}

// 		return -random_dir;
// 	}
// vec3 random_in_unit_sphere() {
// 	double theta = random_double(0,PI);
// 	double phi = random_double(0, 2 * PI); 
// 	return vec3(sin(theta) * sin(phi), cos(theta), sin(theta) * cos(phi));
// }

// bool nearZero(const vec3& v) {
// 	return (fabs(v.x) < 1e-6) && (fabs(v.y) < 1e-6) && (fabs(v.z) < 1e-6);
// }
// double length_squared(const vec3& v) {
// 	return v.x * v.x + v.y * v.y + v.z * v.z;
// }
// vec3 refract(const vec3& uv, const vec3& n, double etai_over_etat) {
// 	double cos_theta = fmin(dot(-uv, n), 1.0);
// 	vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
// 	vec3 r_out_parallel = -sqrt(fabs(1.0 - length_squared(r_out_perp))) * n;
// 	return r_out_perp + r_out_parallel;
// }

// float radians(float degree) {
// 	return degree / 180.0 * PI;
// }

// double vec3::operator[](unsigned int index)const {
// 	switch (index)
// 	{
// 	case 0: return x; 
// 	case 1: return y; 
// 	case 2: return z; 
// 	default:
// 		break;
// 	}
// }
