#define _CRT_SECURE_NO_WARNINGS
#include"PT/PathTracing.h"
//#include"PTVec.h"
#include"PT/PTHeader.h"
#include"PT/PTScene.h"
#include<PT/hittable/Rect.h>
#include<utility>

using namespace PT;
class PT::PTMaterial;
using std::make_shared;
using std::shared_ptr;


void cornell_box(shared_ptr<PTScene> scene) {
	auto renderer = PTRenderer::GetInstance();
	 

	std::shared_ptr<PTCamera> camera = std::make_shared<PTCamera>
		(vec3(278.0f,278.0f,-800.0f),vec3(278.0f,278.0f,0.0f),vec3(0,1,0),40.0,
		600,600);
	
	scene->addCam(camera);
	
	auto red = make_shared<Lambertian>(vec3(.65, .05, .05));
	auto white = make_shared<Lambertian>(vec3(.73, .73, .73));
	auto green = make_shared<Lambertian>(vec3(.12, .45, .15));
	auto light = make_shared<DiffuseLight>(vec3(1.0f, 1.0f, 1.0f) * 1.0f);

	scene->addObject(make_shared<yz_rect>(0, 555, 0, 555, 555, green));
	scene->addObject(make_shared<yz_rect>(0, 555, 0, 555, 0, red));
	scene->addObject(make_shared<xz_rect>(213, 343, 227, 332, 554, light));
	scene->addObject(make_shared<xz_rect>(0, 555, 0, 555, 0, white));
	scene->addObject(make_shared<xz_rect>(0, 555, 0, 555, 555, white));
	scene->addObject(make_shared<xy_rect>(0, 555, 0, 555, 555, white));
	vec3 a = {278, 278, 278};
	vec3 b = {278, 278 + 200, 278 + 200};
	vec3 c = {278 + 200, 278, 278};
	vec3 normal = glm::normalize(glm::cross(b - a, c - a));
	scene->addObject(make_shared<Triangle>(PTVertex{a,normal}, PTVertex{b,normal}, 
		PTVertex{c,normal}, white));

	scene->addLight(make_shared<xz_rect>(213, 343, 227, 332, 554, light));

	std::shared_ptr<Box> b1 = make_shared<Box>(vec3(0.0f,0.0f,0.0f), vec3(165, 330, 165), white);
	std::shared_ptr<Medium> med_b1 = make_shared<Medium>(b1, 0.01, vec3(0.0f, 0.0f, 0.0f));
	std::shared_ptr<Box> b2 = make_shared<Box>(vec3(0, 0, 0), vec3(165, 165, 165), white);

	glm::mat4 model1 = glm::mat4(1.0f);
	model1 = glm::translate(model1, vec3(265.0f, 0.0f, 295.0f));
	model1 = glm::rotate(model1, glm::radians(15.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	std::shared_ptr<TransHittable> b1_model = make_shared<TransHittable>(
		// std::static_pointer_cast<hittable>(med_b1), model1);
		std::static_pointer_cast<hittable>(b1), model1);
	scene->addObject(b1_model);

	glm::mat4 model2 = glm::translate(glm::mat4(1.0f),vec3(130, 0, 65));
	model2 = glm::rotate(model2, glm::radians(-18.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	model2 = glm::scale(model2, {1.5, 0.8, 1.5});
	std::shared_ptr<TransHittable> b2_model = make_shared<TransHittable>(
		std::static_pointer_cast<hittable>(b2), model2);
	scene->addObject(b2_model);
}

void PT::render() {
	srand(time(0));
	int samples = 20;
	int max_depth = 1;

	// std::shared_ptr<PTRenderer> renderer = std::make_shared<PTRenderer>(samples,max_depth);
	auto renderer = PTRenderer::GetInstance();
	renderer->init(samples, max_depth);
	shared_ptr<PTScene> scene = make_shared<PTScene>();

	// world 
	cornell_box(scene);

	scene->buildBVH();

	int threadNum = std::max(0u,std::thread::hardware_concurrency() - 1);
	renderer->GetInstance()->render(scene,threadNum);
	renderer->writeToFile(scene,"./out.ppm");
}

