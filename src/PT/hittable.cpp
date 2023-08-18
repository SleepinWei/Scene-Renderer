#include "PT/hittable.h"
#include "PT/PTAABB.h"
#include "PT/PTMaterial.h"
#include "PT/PTRay.h"
#include "PT/glm_vec3.h"
#include "PT/onb.h"
#include "PT/hittable.h"
#include "PT/hittable.h"


void hittable::addTexture(std::shared_ptr<PTMaterial> &mat)
{
}

Sphere::Sphere(const vec3 &center, double radius, std::shared_ptr<PTMaterial> material) : center(center), radius(radius), mat_ptr(material)
{
}

void hit_record::set_face_normal(const Ray &r, const vec3 &outward_normal)
{
	this->front_face = dot(r.dir, outward_normal) < 0;
	this->normal = front_face ? outward_normal : -outward_normal;
}
hittable_list::hittable_list(std::shared_ptr<hittable> object)
{
	add(object);
}

void hittable_list::clear()
{
	objects.clear();
}

void hittable_list::add(std::shared_ptr<hittable> object)
{
	objects.push_back(object);
}

bool hittable_list::hit(const Ray &r, double t_min, double t_max, hit_record &rec) const
{
	hit_record temp_rec;
	bool hit_anything = false;
	auto closest = t_max;
	for (auto object : objects)
	{
		if (object->hit(r, t_min, closest, temp_rec))
		{
			// if the ray hits anything and t is smaller than
			// the current closest.
			hit_anything = true;
			closest = temp_rec.t;
			rec = temp_rec;
		}
	}
	return hit_anything;
}

bool Sphere::bounding_box(double time0, double time1, AABB &output_box) const
{
	output_box = AABB(
		center - vec3(radius, radius, radius),
		center + vec3(radius, radius, radius));
	return true;
}

bool hittable_list::bounding_box(double time0, double time1, AABB &output_box) const
{
	if (objects.empty())
		return false;

	AABB temp_box;
	bool first_box = true;

	for (const auto &object : objects)
	{
		if (!object->bounding_box(time0, time1, temp_box))
			return false;
		output_box = first_box ? temp_box : AABB::surrounding_box(output_box, temp_box);
		first_box = false;
	}

	return true;
}

void Sphere::get_sphere_uv(const vec3 &p, float &u, float &v)
{
	// p: a given point on the sphere of radius one, centered at the origin.
	// u: returned value [0,1] of angle around the Y axis from X=-1.
	// v: returned value [0,1] of angle from Y=-1 to Y=+1.
	//     <1 0 0> yields <0.50 0.50>       <-1  0  0> yields <0.00 0.50>
	//     <0 1 0> yields <0.50 1.00>       < 0 -1  0> yields <0.50 0.00>
	//     <0 0 1> yields <0.25 0.50>       < 0  0 -1> yields <0.75 0.50>

	auto theta = acos(-p.y);
	auto phi = atan2(-p.z, p.x) + PI;

	u = phi / (2 * PI);
	v = theta / PI;
}

Sphere::~Sphere()
{
}

bool Sphere::hit(const Ray &r, double t_min, double t_max, hit_record &rec) const
{
	float t = -1.0f;
	vec3 ooc = r.orig - center;
	float a = dot(r.dir, r.dir);
	float half_b = dot(ooc, r.dir);
	float c = dot(ooc, ooc) - radius * radius;

	float delta = half_b * half_b - a * c;
	if (delta < 0)
	{
		return false;
	}
	float sqrtdelta = sqrtf(delta);
	float root = (-half_b - sqrtdelta) / a;
	if (root < t_min || t_max < root)
	{
		root = (-half_b + sqrtdelta) / a;
		if (root < t_min || t_max < root)
		{
			return false;
		}
	}
	rec.t = root;
	rec.p = r.at(rec.t);
	vec3 outward_normal = (rec.p - center) / radius;
	rec.set_face_normal(r, outward_normal);
	this->get_sphere_uv(outward_normal, rec.u, rec.v);
	rec.mat_ptr = mat_ptr;
	// rec.T = normalize(vec3(-rec.p.y, rec.p.x, 0));
	// rec.B = normalize(cross(rec.normal, rec.T));

	return true;
}

void Sphere::addTexture(std::shared_ptr<PTMaterial> &mat)
{
	this->mat_ptr = mat;
}

double Sphere::pdf_value(const vec3 &o, const vec3 &v) const
{
	hit_record rec;
	if (!this->hit(Ray(o, v), 0.001, PT_INFINITY, rec))
		return 0;

	auto cos_theta_max = sqrt(1 - radius * radius / glm::dot(center - o, center - o));
	auto solid_angle = 2 * PI * (1 - cos_theta_max);

	return 1 / solid_angle;
}

vec3 Sphere::random(const vec3 &o) const
{
	vec3 direction = center - o;
	auto distance_squared = dot(direction, direction);
	onb uvw;
	uvw.build_from_w(direction);
	return uvw.local(random_to_sphere(radius, distance_squared));
}

Triangle::Triangle(const PTVertex &a, const PTVertex &b, const PTVertex &c, const shared_ptr<PTMaterial> &m)
{
	this->a = a;
	this->b = b;
	this->c = c;
	this->mat_ptr = m;
}

Triangle::~Triangle()
{
}

bool Triangle::hit(const Ray &r, double t_min, double t_max, hit_record &rec) const
{
	vec3 origin = r.orig;
    vec3 d = r.dir;

    vec3 E1 = b.pos - a.pos;
    vec3 E2 = c.pos - a.pos;
    vec3 T = origin - a.pos;
    vec3 P = glm::cross(d, E2);
	vec3 Q = glm::cross(T, E1);

    float denominator = glm::dot(P,E1);

    float t = glm::dot(Q,E2) / denominator;
    float u = glm::dot(P,T) / denominator;
    float v = glm::dot(Q,d) / denominator;

    // hit or not hit && decide inside or not
    if ( t > t_max || t < t_min || u < 0 || u > 1 || v < 0 || v > 1 || u + v > 1)
    {
        return false;
    }

	// Vec3f normal;
	// Vec3f::Cross3(normal, E1, E2);
	// normal.Normalize();

	rec.t = t;
	rec.p = r.at(rec.t);

	vec3 normal = a.normal * (1 - u - v) + b.normal * u + c.normal * v;
	rec.set_face_normal(r, normal);
	rec.u = a.uv.x * (1 - u - v) + b.uv.x * u + c.uv.x * v;
	rec.v = a.uv.y * (1 - u - v) + b.uv.y * u + c.uv.y * v;
	rec.mat_ptr = mat_ptr;

    return true;
}

bool Triangle::bounding_box(double time0, double time1, AABB &output_box) const
{
	vec3 pos_min = glm::min(glm::min(a.pos, b.pos),c.pos);
	vec3 pos_max = glm::max(glm::max(a.pos, b.pos), c.pos);
	output_box = AABB(pos_min, pos_max);
	return true;
}

bool Directional::hit(const Ray &r, double t_min, double t_max, hit_record &rec) const
{
	// ray 的方向是从 光源开始的
	float costheta = glm::dot(r.dir, this->dir);
	if(costheta >= 0 || t_max < PT_INFINITY){
		return false; 
	}

	rec.t = PT_INFINITY;
	rec.mat_ptr = this->mat_ptr;
	rec.p = r.orig + rec.t * r.dir;
	return true;
}

bool Directional::bounding_box(double time0, double time1, AABB &output_box) const
{
    return false;
}
