#include"Box.h"
#include"../PTRay.h"
#include"../PTAABB.h"
#include"../PTRay.h"
#include"Rect.h"

using namespace PT;

Box::Box(){
	model = glm::mat4(1.0f);
}

bool Box::bounding_box(double time0, double time1, AABB& output_box) const {
	//vec3 
	vec3 delta = box_max - box_min; 
	int offset[] = {
		0,0,0,
		0,0,1,
		0,1,0,
		0,1,1,
		1,0,0,
		1,0,1,
		1,1,0,
		1,1,1
	};
	vec3 aabb_min;
	vec3 aabb_max;
	for (int i = 0; i < 8; i++) {
		int index = 3 * i;
		vec3 vertex = box_min +
			vec3(offset[index] * delta.x, 
				offset[index + 1] * delta.y, 
				offset[index + 2] * delta.z);
		vertex = model * glm::vec4(vertex, 1.0f);
		if (i == 0) {
			aabb_max = vertex;
			aabb_min = vertex;
		}
		else {
			aabb_min = glm::min(aabb_min, vertex);
			aabb_max = glm::max(aabb_min, vertex);
		}
	}
	output_box = AABB(aabb_min, aabb_max);
	return true;
}

void Box::setModel(glm::mat4 model) {
	this->model = model;

	// set faces
	for (auto& object : sides.objects) {
		object->setModel(model);
	}
}

Box::Box(const point3& p0, const point3& p1, shared_ptr<Material> ptr) {
	box_min = p0;
	box_max = p1;

	sides.add(make_shared<xy_rect>(p0.x, p1.x, p0.y, p1.y, p1.z, ptr));
	sides.add(make_shared<xy_rect>(p0.x, p1.x, p0.y, p1.y, p0.z, ptr));

	sides.add(make_shared<xz_rect>(p0.x, p1.x, p0.z, p1.z, p1.y, ptr));
	sides.add(make_shared<xz_rect>(p0.x, p1.x, p0.z, p1.z, p0.y, ptr));

	sides.add(make_shared<yz_rect>(p0.y, p1.y, p0.z, p1.z, p1.x, ptr));
	sides.add(make_shared<yz_rect>(p0.y, p1.y, p0.z, p1.z, p0.x, ptr));
}

bool Box::hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const {
	return sides.hit(r, t_min, t_max, rec);
}





