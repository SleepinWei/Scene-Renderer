#include"BVH.h"
PT::BVH_Node::BVH_Node(const hittable_list& list, double time0, double time1):
	BVH_Node(list.objects,0,list.objects.size(),time0,time1) {
	
}
PT::BVH_Node::BVH_Node(
	const std::vector<std::shared_ptr<hittable>>& src_objects,
	size_t start, size_t end, double time0, double time1
) {
	auto objects = src_objects;

	int axis = random_int(0, 2);
	auto comparator = 
		(axis == 0) ? box_x_compare 
		: (axis == 1) ? box_y_compare 
		: box_z_compare;

	size_t object_span = end - start; 
	
	if (object_span == 1) {
		left = right = objects[start]; 
	}
	else if (object_span == 2) {
		if (comparator(objects[start], objects[start + 1])) {
			left = objects[start];
			right = objects[start + 1];
		}
		else {
			left = objects[start + 1];
			right = objects[start]; 
		}
	}
	else {
		std::sort(objects.begin() + start, objects.begin() + end, comparator);

		auto mid = start + object_span / 2; 
		left =std::make_shared<BVH_Node>(objects, start, mid, time0, time1);
		right = std::make_shared<BVH_Node>(objects, mid, end, time0, time1);
	}
	
	AABB box_left, box_right;
	if (!left->bounding_box(time0, time1, box_left) ||
		!right->bounding_box(time0, time1, box_right)) {

	}
	box = surrounding_box(box_left, box_right);
}

bool PT::BVH_Node::hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const {
	if (!box.hit(r, t_min, t_max))
		return false;

	bool hit_left = left->hit(r, t_min, t_max, rec);
	bool hit_right = right->hit(r, t_min, hit_left ? rec.t : t_max, rec);

	return hit_left || hit_right;
}
bool PT::BVH_Node::bounding_box(double time0, double time1, AABB& output_box) const {
	output_box = box;
	return true;
}
inline bool PT::box_compare(const std::shared_ptr<hittable> a, const std::shared_ptr<hittable> b, int axis) {
	AABB box_a;
	AABB box_b;

	if (!a->bounding_box(0, 0, box_a) || !b->bounding_box(0, 0, box_b))
		std::cerr << "No bounding box in bvh_node constructor.\n";

	return box_a.minimum[axis] < box_b.minimum[axis];
}


inline bool PT::box_x_compare(const std::shared_ptr<hittable> a, const std::shared_ptr<hittable> b) {
	return box_compare(a, b, 0);
}

bool PT::box_y_compare(const std::shared_ptr<hittable> a, const std::shared_ptr<hittable> b) {
	return box_compare(a, b, 1);
}

bool PT::box_z_compare(const std::shared_ptr<hittable> a, const std::shared_ptr<hittable> b) {
	return box_compare(a, b, 2);
}

