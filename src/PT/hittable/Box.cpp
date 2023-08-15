#include"Box.h"
#include"PT/PTRay.h"
#include"PT/PTAABB.h"
#include"PT/PTRay.h"
#include"Rect.h"

using namespace PT;

Box::Box(){
	//model = glm::mat4(1.0f);
}

bool Box::bounding_box(double time0, double time1, AABB& output_box) const {
	//vec3 
	output_box = AABB(box_min, box_max);
	return true;
}

//void Box::setModel(glm::mat4 model) {
	//this->model = model;

	// set faces
	//for (auto& object : sides.objects) {
		//object->setModel(model);
	//}
//}

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

bool Box::hit(const Ray& r, double t_min, double t_max, hit_record& rec) const {
	return sides.hit(r, t_min, t_max, rec);
}





