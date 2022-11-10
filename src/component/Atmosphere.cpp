#include<glad/glad.h>
#include<glm/gtc/type_ptr.hpp>
#include"Atmosphere.h"
#include"../buffer/ImageTexture.h"
#include"../buffer/UniformBuffer.h"
#include"../utils/Shader.h"
#include"../system/RenderManager.h"
#include"../utils/Utils.h"

extern std::unique_ptr<RenderManager> renderManager;

Atmosphere::Atmosphere() {
	Component::name = "Atmosphere";
	
	initTextures();
	initAtmosphereParameters();
	initShaders();

	//debug 
	sunAngle = 10.0f;
}
void Atmosphere::initTextures() {
	transmittanceTexture = std::make_shared<ImageTexture>();
	transmittanceTexture->genImageTexture(GL_RGBA32F, GL_RGBA, transWidth, transHeight);
	transmittanceTexture->setBinding(0);

	skyViewTexture = std::make_shared<ImageTexture>();
	skyViewTexture->genImageTexture(GL_RGBA32F, GL_RGBA, skyViewWidth, skyViewHeight);
	skyViewTexture->setBinding(1);

	const int ParameterSize = 100;
	atmBuffer = std::make_shared<UniformBuffer>(ParameterSize);
	atmBuffer->setBinding(7);
}

void Atmosphere::initAtmosphereParameters(){
	// initialize AtmosphereParameters
	atmosphere.solar_irradiance = 10.0f; // solar irradiance
	atmosphere.sun_angular_radius = 0.005f;//sun angular radius
	atmosphere.top_radius = 6460.0f;// top_radius
	atmosphere.bottom_radius = 6360.0f;// bottom_radius

	atmosphere.HDensityRayleigh = 8.0f; //hdensity rayleigh
	atmosphere.HDensityMie = 1.2f; // hdensity mie
	atmosphere.OzoneCenter = 25.0f; //ozoneCenter
	atmosphere.mie_g = 0.8;//mig_g
	atmosphere.rayleigh_scattering = glm::vec3(5.802e-3, 13.558e-3, 33.1e-3);//ray_scat
	// here 1 means 1km, thus we make scattering & absorption 1000 times larger
	atmosphere.mie_scattering = glm::vec3(3.996e-3, 3.996e-3, 3.996e-3);//sattering
	atmosphere.mie_extinction = glm::vec3(4.40e-3, 4.40e-3, 4.40e-3);//mie_absorption(extinction)
	atmosphere.mie_extinction = atmosphere.mie_extinction + atmosphere.mie_scattering;
	atmosphere.absorption_extinction = glm::vec3(0.650e-3, 1.881e-3, 0.085e-3);// ozone absorption
	atmosphere.OzoneWidth = 15.0f; //ozone thickness
}

void Atmosphere::initShaders() {
	// TODO: add compute shader to RenderManager;
	compTransShader = std::make_shared<Shader>("./src/shader/sky/transmittance.comp");
	compskyViewShader = std::make_shared<Shader>("./src/shader/sky/skyview.comp");
	shader = renderManager->getShader(ShaderType::SKY);
	// only for debug
	//shader = renderManager->getShader(ShaderType::TEST);
}

void Atmosphere::prepareAtmosphere() {
	if (atmBuffer->dirty) {
		atmBuffer->setDirtyFlag(false);

		AtmosphereParameters& data = this->atmosphere;
		atmBuffer->bindBuffer();
		glBufferSubData(GL_UNIFORM_BUFFER, 0, 4, &data.solar_irradiance);
		glBufferSubData(GL_UNIFORM_BUFFER, 4, 4, &data.sun_angular_radius);
		glBufferSubData(GL_UNIFORM_BUFFER, 8, 4, &data.top_radius);
		glBufferSubData(GL_UNIFORM_BUFFER, 12, 4, &data.bottom_radius);

		glBufferSubData(GL_UNIFORM_BUFFER, 16, 4, &data.HDensityRayleigh);
		glBufferSubData(GL_UNIFORM_BUFFER, 20, 4, &data.HDensityMie);
		glBufferSubData(GL_UNIFORM_BUFFER, 24, 4, &data.OzoneCenter);
		glBufferSubData(GL_UNIFORM_BUFFER, 28, 4, &data.mie_g);

		glBufferSubData(GL_UNIFORM_BUFFER, 32, 16, glm::value_ptr(data.rayleigh_scattering));
		glBufferSubData(GL_UNIFORM_BUFFER, 48, 16, glm::value_ptr(data.mie_scattering));
		glBufferSubData(GL_UNIFORM_BUFFER, 64, 16, glm::value_ptr(data.mie_extinction));
		glBufferSubData(GL_UNIFORM_BUFFER, 80, 16, glm::value_ptr(data.absorption_extinction));

		//ozone width 
		glBufferSubData(GL_UNIFORM_BUFFER, 96, 4, &data.OzoneWidth);

		////debug
		//atmBuffer->bindBuffer();
		//float* content = (float*)glMapBuffer(GL_UNIFORM_BUFFER, GL_READ_ONLY);
		//for (int i = 0; i < 30; i++) {
		//	std::cout << content[i] << '\n';
		//}
	}
}

void Atmosphere::computeTransTexture() {
	//set binding
	transmittanceTexture->setBinding(0);
	static bool firstCall = true;
	if (firstCall) {
		compTransShader->use();
		compTransShader->setInt("TexWidth", transWidth);
		compTransShader->setInt("TexHeight", transHeight);
		firstCall = false;
	}
	compTransShader->use();
	glDispatchCompute(transWidth, transHeight, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
#define PI 3.1415926
void Atmosphere::computeSkyViewTexutre() {
	//set binding
	skyViewTexture->setBinding(1);
	static bool firstCall = true;
	if (firstCall) {
		firstCall = false;
		compskyViewShader->use();
		compskyViewShader->setInt("transTexWidth", transWidth);
		compskyViewShader->setInt("transTexHeight", transHeight);
		compskyViewShader->setInt("TexWidth", skyViewWidth);
		compskyViewShader->setInt("TexHeight", skyViewHeight);
	}
	compskyViewShader->use();
	//TODO: sunposition

	//debug
	compskyViewShader->setFloat("sunAngle", sunAngle);
	compskyViewShader->setInt("transmittance_texture", 0);
	glActiveTexture(GL_TEXTURE0);
	transmittanceTexture->bindBuffer();
	// debug end
	glDispatchCompute(skyViewWidth, skyViewHeight, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
void Atmosphere::computeDrawCall() {
	computeTransTexture();
	computeSkyViewTexutre();
}

void Atmosphere::renderDrawCall(bool useShader) {
	glDepthFunc(GL_LEQUAL);
	glCullFace(GL_FRONT);

	glActiveTexture(GL_TEXTURE8);
	skyViewTexture->bindBuffer();

	if (useShader) {
		shader->use();
		shader->setInt("skyViewLut", 8);
		//shader->setInt("tex", 8);
	}
	renderCube();
	// only for debug
	//renderQuad();
	glCullFace(GL_BACK);
	glDepthFunc(GL_LESS);
}


void Atmosphere::render(bool useShader) {
	renderDrawCall(useShader);
}

Atmosphere::~Atmosphere() {

}

void Atmosphere::constructCall() {
	prepareAtmosphere();
	computeDrawCall();
}