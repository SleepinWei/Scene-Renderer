#include<glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include<iostream>
#include<glfw/glfw3.h>
#include<glm/glm.hpp>
#include"utils/Utils.h"
#include"utils/Camera.h"
#include"component/GameObject.h"
#include"GUI.h"
#include"object/SkyBox.h"
#include"object/Terrain.h"
#include"renderer/Material.h"
#include"component/Lights.h"
#include"renderer/ResourceManager.h"
#include"system/InputManager.h"
extern "C" __declspec(dllexport) long long NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;

const unsigned int  SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 900;

// manager
std::unique_ptr<RenderManager> renderManager;
std::unique_ptr<ResourceManager> resourceManager;
std::unique_ptr<InputManager> inputManager;

#define TEST
#ifndef TEST

void render() {
	glfwInit();
	GLFWwindow* window; 
	createWindow(window, SCR_WIDTH, SCR_HEIGHT);
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	
	//glad
	gladInit();
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glEnable(GL_PROGRAM_POINT_SIZE);
	Gui gui(window);

	Shader lightShader("./src/shader/light.vs", "./src/shader/light.fs");
	//Shader skyboxShader("./src/shader/skybox.vs", "./src/shader/skybox.fs");
	//skyboxShader.use();
	//skyboxShader.setInt("skybox", 0);
	
	unsigned int cubeVAO;
	cubeVAO = createCube();

	//Sphere sphere(TEX_TYPE::METAL);
	//Plane plane;
	std::shared_ptr<SkyBox> skybox = std::make_shared<SkyBox>();
	skybox->addShader(ShaderType::SKYBOX)
		->addMaterial("./asset/skybox/");
		
	PointLight light;
	//Terrain terrain("./asset/heightmap/iceland.png","./asset/heightmap/iceland_normal.png",
		//"./asset/pbr/sand/");
	std::shared_ptr<Terrain> terrain = std::make_shared<Terrain>();
	terrain->loadHeightmap("./asset/heightmap/iceland/");
	auto terrainMaterial = Material::loadPBR("./asset/pbr/sand/");
	terrain->addMaterial(terrainMaterial);
	terrain->addShader(ShaderType::TERRAIN);

	std::vector<std::shared_ptr<GameObject>> objects; 
	objects.push_back(terrain);

	float lightColor = 1.0; 
	while (!glfwWindowShouldClose(window)) {
		gui.window(light.lightPos,lightColor);

		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glClearColor(0.6, 0.6, 0.6, 1.0);

		processInput(window);
		glm::mat4 projection, view; 

		//glm::vec3 lightColor = glm::vec3(1.0, 1.0, 1.0);

		view = camera.GetViewMatrix();
		//projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);
		projection = camera.GetPerspective();
		glm::vec3 planeColor = glm::vec3(0.3f, .5f, 0.2f);

		//plane
		//simpleShader.use();
		//simpleShader.setMat4("view", view);
		//simpleShader.setMat4("projection", projection);
		//simpleShader.setVec3("light.Color",glm::vec3(lightColor));
		//simpleShader.setVec3("light.Pos",light.lightPos);
		//simpleShader.setVec3("viewPos", camera.Position);
		//simpleShader.setVec3("objectColor", planeColor);
		//plane.render(simpleShader);

		// light cube 
		lightShader.use();
		lightShader.setMat4("view", view);
		lightShader.setMat4("projection", projection);
		lightShader.setFloat("lightColor", lightColor);
		light.render(lightShader);

		//pbrShader.use();
		//pbrShader.setMat4("projection", projection);
		//pbrShader.setMat4("view", view);
		//pbrShader.setVec3("light.Color", lightColor);
		//pbrShader.setVec3("light.Position", light.lightPos);
		//pbrShader.setVec3("camPos", camera.Position);
		//sphere.render(pbrShader);

		// terrain 
		/*terrainShader.use();
		terrainShader.setMat4("projection", projection);
		terrainShader.setMat4("view", view);
		terrainShader.setVec3("light.Position", light.lightPos);
		terrainShader.setFloat("light.Color", lightColor);
		terrainShader.setVec3("viewPos", camera.Position);
		terrain.render(terrainShader);*/
		renderManager.updateShader(camera); // 重新设计为 uniform buffer object

		renderManager.render(objects,light.lightPos,glm::vec3(lightColor));

		view = glm::mat4(glm::mat3(camera.GetViewMatrix()));
		//skyboxShader.use();
		//skyboxShader.setMat4("view", view);
		//skyboxShader.setMat4("projection", projection);
		skybox->render();
		
		gui.render();
		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	glDeleteVertexArrays(1, &cubeVAO);
	//glDeleteBuffers()
	gui.destroy();
	glfwDestroyWindow(window);
	glfwTerminate();

}
#endif

void test_print(glm::mat4& model) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			std::cout << model[i][j] << ' '; 
		}
		std::cout << '\n';
	}
}
void test() {
	struct Data {
		glm::vec3 a;
		glm::vec3 b;
		glm::vec3 c;
		float d;
		float e;
		float f;
	};
	std::cout << "Hello" << '\n';
	std::cout << sizeof(Data) << '\n';
	std::cout << offsetof(Data, a) << '\n';
	std::cout << offsetof(Data, b) << '\n';
	std::cout << offsetof(Data, c) << '\n';
	std::cout << offsetof(Data, d) << '\n';
	std::cout << offsetof(Data, e) << '\n';
	std::cout << offsetof(Data, f) << '\n';
}

//#include"PathTracing.h"
int main() {
	// 
	//render();
	test();
	//PT::render();
	return 0; 
}