#pragma once
#include<glm/glm.hpp>
using glm::vec3;

struct Frustum;

class AABB {
public:
	AABB() = default;
	AABB(const vec3& min, const vec3& max);

	static AABB surrounding_box(const AABB& box0, const AABB& box1);
public:
	vec3 minimum;
	vec3 maximum;
};

class BoundingSphere {
public:
	BoundingSphere();
	BoundingSphere(const BoundingSphere& s);
	BoundingSphere(const vec3& center, float radius);
	static BoundingSphere surrounding_sphere(const BoundingSphere& s1, const BoundingSphere& s2);
public:
	vec3 center;
	float raidus;
	bool initDone;
};

struct CameraPlane{
	vec3 normal;
	vec3 closestPoint;
	float distance; // distance from origin to nearest point in the plane
};

struct Frustum {
	CameraPlane topFace;
	CameraPlane bottomFace;

	CameraPlane rightFace;
	CameraPlane leftFace;

	CameraPlane farFace;
	CameraPlane nearFace;
};

