#include"PathTracing.h"
#include"PTVec.h"
#include"PTCamera.h"
#include<iostream>
#include"PTrandom.h"

PT::Sphere::Sphere(vec3 center,double radius):
	center(center),radius(radius) {
	
}

PT::Sphere::~Sphere() {

}

bool PT::Sphere::hit(Ray& r,double t_min,double t_max,hitRecord& rec){
	double t = -1.0f;
	vec3 ooc = r.orig - center; 
	double a = dot(r.dir, r.dir);
	double b = 2.0f * dot(ooc, r.dir);
	double c = dot(ooc, ooc) - radius * radius;

	double delta = b * b - 4 * a * c;
	if (delta < 1e-6) {
		return false; 
	}
	double sqrtdelta = sqrt(delta);
	double root = (-b - sqrtdelta) / (2.0 * a);
	if (root < t_min || t_max < root) {
		root = (- b + sqrtdelta) / (2.0 * a);
		if (root < t_min || t_max < root) {
			return false;
		}
	}
	rec.t = root;
	rec.p = r.at(rec.t);
	vec3 outward_normal = (rec.p - center) / radius; 
	rec.set_face_normal(r, outward_normal);
	rec.T = normalize(vec3(-rec.p.y, rec.p.x, 0));
	rec.B = normalize(cross(rec.normal, rec.T));

	return true;
}

PT::vec3 TBN(PT::vec3 v, PT::vec3& T, PT::vec3& B, PT::vec3& N) {
	return PT::vec3(
		v.x * T.x + v.y * B.x + v.z * N.x,
		v.x * T.y + v.y * B.y + v.z * N.y,
		v.x * T.z + v.y * B.z + v.z * N.z
	);
}

PT::vec3 random_in_hemisphere(PT::vec3& normal) {
	PT::vec3 random_dir = PT::random_direction();
	if (dot(random_dir, normal) > 0.0) {
		return random_dir;
	}
	return -random_dir;
}

PT::vec3 PT::rayColor(Ray r,hittable& world,int depth) {
	hitRecord rec;

	if (depth <= 0) {
		return vec3(0, 0, 0);
	}

	bool isHit = world.hit(r,0,infinity,rec);
	if (isHit) {
		vec3 new_ray_dir = random_in_hemisphere(rec.normal);
		//new_ray_dir = TBN(new_ray_dir, rec.T, rec.B, rec.normal);
		return 0.5 * rayColor(Ray(rec.p, new_ray_dir), world, depth - 1);
	}
	vec3 unit_direction = normalize(r.dir);
	auto t = 0.5 * (unit_direction.y + 1.0);
	return (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
}

void PT::hitRecord::set_face_normal(Ray& r, vec3& outward_normal) {
	this->front_face = dot(r.dir, outward_normal) < 0;
	this->normal = front_face ? outward_normal : -outward_normal;
}


void write_color(PT::vec3 color, int samples_per_pixel) {
	auto r = color.x; 
	auto g = color.y;
	auto b = color.z;

	auto scale = 1.0 / samples_per_pixel;

	r = sqrt(scale * r);
	g = sqrt(scale * g);
	b = sqrt(scale * b);

	std::cout << int(256 * clamp(r, 0.0, 0.999)) << ' '
		<< int(256 * clamp(g, 0.0, 0.999)) << ' '
		<< int(256 * clamp(b, 0.0, 0.999)) << '\n';
}
void PT::render() {
	freopen("./out.ppm", "w", stdout);
	Sphere sphere(vec3(0, 0, -10), 5);

	// image plane  
	Camera camera;
	int h = 450;
	int w= int(camera.aspect_ratio * h);

	// world 
	hittable_list world;
	Sphere s1(vec3(0, 0, -1), 0.5);
	Sphere s2(vec3(0, -100.5, -1), 100);
	world.add(&s1);
	world.add(&s2);

	int samples_per_pixel = 20;
	int max_depth = 10;

	std::cout << "P3\n" << w << ' ' << h << "\n255\n";

	for (int j = h-1; j >=0; j--) {
		std::cerr << "Progress: " << (h-1-j) *1.0/ (h-1) *100 << "%\n";
		for (int i = 0; i < w; i++) {
			vec3 color(0, 0, 0);
			for (int s = 0; s < samples_per_pixel; s++) {
				auto u = (i + random_double()) / (w - 1);
				auto v = (j + random_double()) / (h - 1);
				Ray r = camera.get_ray(u, v);
				color = color + rayColor(r, world,max_depth);
			}
			write_color(color, samples_per_pixel);
		}
	}
	fclose(stdout);
}

PT::hittable_list::hittable_list(hittable* object) {
	add(object);
}

void PT::hittable_list::clear() {
	objects.clear();
}

void PT::hittable_list::add(hittable* object) {
	objects.push_back(object);
}

bool PT::hittable_list::hit(Ray& r, double t_min, double t_max, hitRecord& rec) {
	hitRecord temp_rec; 
	bool hit_anything = false;
	auto closest = t_max;
	for (hittable* object : objects) {
		if (object->hit(r, t_min, closest, temp_rec)) {
			// if the ray hits anything and t is smaller than 
			// the current closest. 
			hit_anything = true;
			closest = temp_rec.t;
			rec = temp_rec;
		}
	}
	return hit_anything;
}