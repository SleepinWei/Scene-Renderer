#include "PT/hittable/TransHittable.h"
#include "PT/PTAABB.h"
#include "PT/PTRay.h"

using namespace PT;

TransHittable::TransHittable()
{
	model = glm::mat4(1.0f);
	object = nullptr;
}

TransHittable::TransHittable(std::shared_ptr<hittable> object, glm::mat4 model)
{
	this->model = model;
	this->object = object;
}

TransHittable::~TransHittable()
{
}

bool TransHittable::hit(const Ray &r, double t_min, double t_max, hit_record &rec) const
{
	if (object == nullptr)
	{
		return false;
	}
	glm::mat4 inv_model = glm::inverse(this->model);
	Ray local_r;
	hit_record local_rec;

	local_r.orig = inv_model * glm::vec4(r.orig, 1.0f);
	local_r.dir = inv_model * glm::vec4(r.dir, 0.0f);
	// bool isHit = object->hit(local_r, t_min, t_max, rec);
	float len = sqrtf(glm::dot(local_r.dir,local_r.dir));
	local_r.dir = glm::normalize(local_r.dir);

	bool isHit = object->hit(local_r, t_min, rec.t * len, local_rec);

	if (!isHit)
	{
		return false;
	}

	double newt = local_rec.t / len;
	vec3 newp = model * glm::vec4(local_rec.p, 1.0f);
	// if (fabs(r.dir.x) > 1e-6)
	// {
		// newt = (newp.x - r.orig.x) / (r.dir.x);
	// }
	// else if (fabs(r.dir.y) > 1e-6)
	// {
		// newt = (newp.y - r.orig.y) / (r.dir.y);
	// }
	// else
	// {
		// newt = (newp.z - r.orig.z) / (r.dir.z);
	// }

	if (rec.t < newt)
	{
		return false;
	}

	rec = local_rec;
	rec.p = newp;
	rec.t = newt;
	rec.normal = glm::normalize(glm::mat3(glm::transpose(inv_model)) * local_rec.normal);
	return isHit;
}

bool TransHittable::bounding_box(double time0, double time1, AABB &output_box) const
{
	AABB child_aabb;
	bool returnVal = object->bounding_box(time0, time1, child_aabb);
	if (!returnVal)
	{
		return false;
	}

	vec3 box_max = child_aabb.max(), box_min = child_aabb.min();
	vec3 delta = box_max - box_min;
	int offset[] = {
		0, 0, 0,
		0, 0, 1,
		0, 1, 0,
		0, 1, 1,
		1, 0, 0,
		1, 0, 1,
		1, 1, 0,
		1, 1, 1};
	vec3 aabb_min;
	vec3 aabb_max;
	for (int i = 0; i < 8; i++)
	{
		int index = 3 * i;
		vec3 vertex = box_min +
					  vec3(offset[index] * delta.x,
						   offset[index + 1] * delta.y,
						   offset[index + 2] * delta.z);
		vertex = this->model * glm::vec4(vertex, 1.0f);
		if (i == 0)
		{
			aabb_max = vertex;
			aabb_min = vertex;
		}
		else
		{
			aabb_min = glm::min(aabb_min, vertex);
			aabb_max = glm::max(aabb_min, vertex);
		}
	}
	output_box = AABB(aabb_min, aabb_max);
	return returnVal;
}

void TransHittable::addTexture(std::shared_ptr<PTMaterial> &mat)
{
	if (object)
		object->addTexture(mat);
}
