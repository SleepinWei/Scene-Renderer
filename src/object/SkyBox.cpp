#include<glad/glad.h>
#include<stb/stb_image.h>
#include"object/SkyBox.h"
#include"renderer/Texture.h"
#include"system/RenderManager.h"
#include"renderer/Material.h"
#include"component/Atmosphere.h"

extern std::unique_ptr<RenderManager> renderManager;
std::shared_ptr<SkyBox> SkyBox::addShader(ShaderType st) {
	shader = renderManager->getShader(st);
	return std::dynamic_pointer_cast<SkyBox> (shared_from_this());
}

std::shared_ptr<SkyBox> SkyBox::addMaterial(const std::string& path) {
	material = Material::loadCubeMap(path);
	return std::dynamic_pointer_cast<SkyBox> (shared_from_this());
}

SkyBox::SkyBox() {
	name = "Skybox";
	VAO = VBO = 0;
	init();
}

void SkyBox::init(){
	float skyboxVertices[] = {
		// positions          
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		-1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f
	};
	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);
	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
}

SkyBox::~SkyBox() {
	glDeleteBuffers(1, &VBO);
	glDeleteVertexArrays(1, &VAO);
}

void SkyBox::render()const {
	glDepthFunc(GL_LEQUAL);

	glBindVertexArray(VAO);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, material->textures[0]->id);
	shader->setInt("skybox", 0);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);

	glDepthFunc(GL_LESS);
}

Sky::Sky() {
	//atmosphere = std::make_shared<Atmosphere>();
	skybox = std::make_shared<Material>();
}

Sky::~Sky() {

}

void Sky::initSkyBoxTexture() {
	// init skybox texture
	if (skybox && !skybox->initDone) {
		skybox->initDone = true;

		auto& textureID = skybox->textures["skybox"]->id;
		glGenTextures(1, &textureID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

		for (unsigned int i = 0; i < 6; i++)
		{
			// load as 3 channel
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
				0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data[i]
			);
			stbi_image_free(data[i]);
		}
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	}
}
void Sky::render(const std::shared_ptr<Shader>& shader) {
	initSkyBoxTexture();
	auto&& atmosphere = std::static_pointer_cast<Atmosphere>(GameObject::GetComponent("Atmosphere"));
	if (atmosphere) {
		atmosphere->render(shader);
	}
}

void Sky::loadSkyBox(const std::string& folder_path) {
	std:: vector<std::string> faces
	{
		folder_path + "right.png",
		folder_path + "left.png",
		folder_path + "top.png",
		folder_path + "bottom.png",
		folder_path + "front.png",
		folder_path + "back.png"
	};

	std::shared_ptr<Texture> tex = std::make_shared<Texture>();
	//auto& textureID = tex->id;
	//glGenTextures(1, &textureID);
	//glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

	//int width, height, nrChannels;
	int nrChannels;
	for (unsigned int i = 0; i < faces.size(); i++)
	{
		// load as 3 channel
		//stbi_set_flip_vertically_on_load(true);
		this->data[i] = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 3);
		if (data)
		{
			//glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
				//0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data
			//);
			//stbi_image_free(data);
		}
		else
		{
			std::cout << "Cubemap tex failed to load at path: " << faces[i] << std::endl;
			stbi_image_free(data[i]);
		}
	}
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	tex->name = folder_path; 
	skybox = std::make_shared<Material>();
	skybox->addTexture(tex,"skybox");
	skybox->initDone= false;
}

void Sky::loadFromJson(json& data) {
	this->GameObject::addComponent(std::make_shared<Atmosphere>());
	if (data.find("skybox") != data.end()) {
		this->loadSkyBox(data["skybox"].get<std::string>());
	}
}
