#pragma once
#include<memory>
#include<glm/glm.hpp>
#include"./Component.h"

struct AtmosphereParameters; 
typedef unsigned int GLuint;

class ImageTexture;
class UniformBuffer;
class Shader;

struct AtmosphereParameters {
	// solar irradiance at the top of the atmosphere
	float solar_irradiance; //4
	float sun_angular_radius; //8
	float top_radius; //12
	float bottom_radius;//16

	// DensityProfile rayleigh_density; 
	float HDensityRayleigh; //20
	// DensityProfile mie_density; 
	float HDensityMie;//24
	float OzoneCenter;//28

	// g coefficient in phase function
	float mie_g; //32

	//scattering coefficient of air molecules
	glm::vec3 rayleigh_scattering;// 32 + 16 = 48
	glm::vec3 mie_scattering; // 48 + 16 = 64
	glm::vec3 mie_extinction;// 64 + 16 = 80

	// DensityProfile absorption_density; //ozone layer
	glm::vec3 absorption_extinction; // 80 + 16 = 96
	float OzoneWidth;//96 + 4 = 100

	// vec3 ground_albedo; // average albedo (can be replaced by gorund textures? perhaps)

	// cosine of maximum sun zenith angle. 
	// which is the smallest sun zenith angle yielding negligible 
	// sky light radiance values. Earth: 102 degress -> -0.2 
	// float mu_s_min;
};

class Atmosphere :public Component,public std::enable_shared_from_this<Atmosphere>{
public:
	Atmosphere();
	~Atmosphere();
	void render(const std::shared_ptr<Shader>& shader); 
	void constructCall();

public:
	// members
	AtmosphereParameters atmosphere;
	std::shared_ptr<ImageTexture> transmittanceTexture;
	std::shared_ptr<ImageTexture> skyViewTexture;
	std::shared_ptr<ImageTexture> multiTexture;
	std::shared_ptr<ImageTexture> convolutionTexture;
	std::shared_ptr<UniformBuffer> atmBuffer;

	//shader 
	std::shared_ptr<Shader> compTransShader;
	std::shared_ptr<Shader> compskyViewShader;
	std::shared_ptr<Shader>compMultiShader;
	std::shared_ptr<Shader> shader;
	std::shared_ptr<Shader> compConvolutionShader;

	// const members
	const unsigned int skyViewWidth = 200;
	const unsigned int skyViewHeight = 100;
	const unsigned int transWidth = 256;
	const unsigned int transHeight = 64;
	const unsigned int multiWidth = 32;
	const unsigned int multiHeight = 32;

	//debug 
	float sunAngle;

private:
	void computeTransTexture();
	void computeSkyViewTexutre();
	void computeMultiTexture();
	void initShaders(); 
	void initAtmosphereParameters();
	void initTextures();
	void prepareAtmosphere();
	void computeDrawCall(); 
	void renderDrawCall(const std::shared_ptr<Shader>& shader);
	bool initDone; // whether initShaders & initTextures is called
};

