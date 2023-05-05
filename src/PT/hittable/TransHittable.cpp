#include"TransHittable.h"
#include"PT/PTRay.h"
#include"PT/PTAABB.h"

using namespace PT;

TransHittable::TransHittable() {
	model = glm::mat4(1.0f);
	object = nullptr;
}

TransHittable::TransHittable(std::shared_ptr<hittable> object, glm::mat4 model) {
	this->model = model;
	this->object = object;
}

TransHittable::~TransHittable() {

}

bool TransHittable::hit(const Ray& r, double t_min, double t_max, hitRecord& rec)const {
	if (object == nullptr) {
		return false;
	}
	glm::mat4 inv_model = glm::inverse(this->model);
	Ray local_r;

	local_r.orig = inv_model * glm::vec4(r.orig, 1.0f);
	local_r.dir = inv_model * glm::vec4(r.dir, 0.0f);
	bool isHit = object->hit(local_r, t_min, t_max, rec);

	rec.p = model * glm::vec4(rec.p, 1.0f);
	rec.normal = glm::normalize(glm::vec3(
		glm::transpose(inv_model) * glm::vec4(rec.normal, 0.0f))
	);
	return isHit;
}

bool TransHittable::bounding_box(double time0, double time1, AABB& output_box)const {
	AABB child_aabb;
	bool returnVal = object->bounding_box(time0, time1, child_aabb);

	if (returnVal) {
		vec3 box_max = child_aabb.max(), box_min = child_aabb.min();
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
			vertex = this->model * glm::vec4(vertex, 1.0f);
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
	}
	return returnVal;
}

void TransHittable::addTexture(std::shared_ptr<Material>& mat) {
	if (object)
		object->addTexture(mat);
}

