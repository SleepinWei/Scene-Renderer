#define STB_IMAGE_IMPLEMENTATION
#include<glad/glad.h>
#include<iostream>
#include<glfw/glfw3.h>
#include<glm/glm.hpp>
#include"src/Utils.h"
#include"src/Camera.h"
#include"Object.h"
#include"GUI.h"
#include"SkyBox.h"
#include"Terrain.h"
#include"Material.h"

const unsigned int  SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 900;

Camera camera(glm::vec3(.0f, .0f, 0.f));
float lastX;
float lastY;
bool firstMouse;

// time 
float deltaTime;
float lastFrame;

// manager
RenderManager renderManager;
ResourceManager resourceManager;

//cubes 

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

	//Shader simpleShader("./shader/simple.vs", "./shader/simple.fs");
	//Shader pbrShader("./shader/pbr.vs", "./shader/pbr.fs");
	Shader lightShader("./shader/light.vs", "./shader/light.fs");
	Shader skyboxShader("./shader/skybox.vs", "./shader/skybox.fs");
	//Shader terrainShader("./shader/terrain.vs", "./shader/pbr.fs", nullptr, "./shader/terrain.tesc", "./shader/terrain.tese");
	skyboxShader.use();
	skyboxShader.setInt("skybox", 0);
	
	unsigned int cubeVAO;
	cubeVAO = createCube();

	//Sphere sphere(TEX_TYPE::METAL);
	Plane plane;
	SkyBox skybox("./asset/skybox/");
	PointLight light;
	//Terrain terrain("./asset/heightmap/iceland.png","./asset/heightmap/iceland_normal.png",
		//"./asset/pbr/sand/");

	//std::shared_ptr<Terrain> terrain= std::make_shared<Terrain>(TEX_TYPE::SAND,
		//TEX_TYPE::HEIGHT,ShaderType::TERRAIN);
	std::shared_ptr<Sphere> sphere = std::make_shared<Sphere>(TEX_TYPE::METAL);
	std::vector<std::shared_ptr<Renderable>> objects; 
	objects.push_back(sphere);

	float lightColor = 1.0; 
	while (!glfwWindowShouldClose(window)) {
		gui.window(light.lightPos,lightColor);

		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glClearColor(0.6, 0.6, 0.6, 0.6);

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
		renderManager.updateShader(camera);

		renderManager.render(objects,light.lightPos,glm::vec3(lightColor));

		view = glm::mat4(glm::mat3(camera.GetViewMatrix()));
		skyboxShader.use();
		skyboxShader.setMat4("view", view);
		skyboxShader.setMat4("projection", projection);
		skybox.render(skyboxShader);
		
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

void test_print(glm::mat4& model) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			std::cout << model[i][j] << ' '; 
		}
		std::cout << '\n';
	}
}
void test() {
	glm::mat4 model = glm::mat4(1); 
	model = glm::translate(model, glm::vec3(1, 1, 1));
	test_print(model);
	model = glm::mat4(1);
	model = glm::translate(model, glm::vec3(1, 1, 1));
	model = glm::scale(model, glm::vec3(0.2));
	test_print(model);
}

#include"PathTracing.h"
int main() {
	// 
	render();
	//test();
	//PT::render();
	return 0; 
}