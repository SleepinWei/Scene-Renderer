#pragma once

#include<string>
#include<vector>
#include"../component/Component.h"
#include<memory>


#include "../buffer/ImageTexture.h"
#include "../utils/Shader.h"

class Material;
enum class ShaderType;
class Shader;
class SSBO;
class ImageTexture;
class Component;


class Ocean :public Component, public std::enable_shared_from_this<Ocean>
{
public:
	Ocean();
	~Ocean();
    void render();//外部接口
    
//origin
public:
    // texture
    std::shared_ptr<ImageTexture> GaussianRandomRT_Texture;
    std::shared_ptr<ImageTexture> HeightSpectrumRT_Texture;
    std::shared_ptr<ImageTexture> DisplaceXSpectrumRT_Texture;
    std::shared_ptr<ImageTexture> DisplaceZSpectrumRT_Texture;
    std::shared_ptr<ImageTexture> InputRT_Texture;
    std::shared_ptr<ImageTexture> OutputRT_Texture;
    std::shared_ptr<ImageTexture> DisplaceRT_Texture;
    std::shared_ptr<ImageTexture> NormalRT_Texture;
    std::shared_ptr<ImageTexture> BubblesRT_Texture;

    //shaders
    std::shared_ptr<Shader> GaussianRandomRT_Shader;
    std::shared_ptr<Shader> DisplaceSpectrum_Shader;
    std::shared_ptr<Shader> HeightSpectrum_Shader;
    std::shared_ptr<Shader> FFTHorizontal_Shader;
    std::shared_ptr<Shader> FFTHorizontalEnd_Shader;
    std::shared_ptr<Shader> FFTVertical_Shader;
    std::shared_ptr<Shader> FFTVerticalEnd_Shader;
    std::shared_ptr<Shader> TextureDisplace_Shader;
    std::shared_ptr<Shader> TextureNormalBubbles_Shader;

    std::shared_ptr<Shader> draw_shader;

public:
    const int FFTPow = 9;         //生成海洋纹理大小 2的次幂，例 为10时，纹理大小为1024*1024
    const int fft_size = 512;//fft纹理大小 = pow(2,FFTPow)
    int MeshSize = 100;		//网格长宽数量
    float MeshLength = 512;	//网格长度
    float TimeScale = 2;     //时间影响
    glm::vec4 WindAndSeed = glm::vec4(1.0f, 1.0f, 0, 0);//风向和随机种子 xy为风, zw为两个随机种子
    // timing
    float inner_time = 0.0f;//时间
    float deltaTime = 0.0f;	// time between current frame and last frame
    float lastFrame = 0.0f;

    //fs parameter-may revised in control panel
    float A = 73;			//phillips谱参数，影响波浪高度
    float Lambda = 8;       //用来控制偏移大小
    float HeightScale = 28.8;   //高度影响
    float BubblesScale = 1.0f;  //泡沫强度1.0
    float BubblesThreshold = 0.86f;//泡沫阈值 0.86
    float WindScale = 30;     //风强

    float outer_FresnelScale=0.02f;
    glm::vec3 outer_OceanColorShallow = pow(glm::vec3(0.30713776f, 0.4703595f, 0.5471698f), glm::vec3(2.2f, 2.2f, 2.2f));
    glm::vec3 outer_OceanColorDeep=pow(glm::vec3(0.0499288f, 0.1436479f, 0.20754719f), glm::vec3(2.2f, 2.2f, 2.2f));
    glm::vec3 outer_BubblesColor = pow(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(2.2f, 2.2f, 2.2f));
    glm::vec3 outer_Specular = pow(glm::vec3(0.3962264f, 0.3943574f, 0.3943574f), glm::vec3(2.2f, 2.2f, 2.2f));
    int outer_Gloss = 256;//256
    glm::vec3 outer_ambient = pow(glm::vec3(0, 0, 0) / 256.0f, glm::vec3(2.2f, 2.2f, 2.2f));//material.ka

private:
    //Mesh data
    std::vector<unsigned int>vertexIndexs;		//网格三角形索引EBO
    //std::vector<glm::vec3> positions;    //位置 vertex coordinates
    //std::vector<glm::vec2>uvs; 			//uv坐标 texture coordinates
    std::vector<float> vertexInfo;//positions uvs

    unsigned int VAO, texture_VAO, VBO, EBO;
    bool initDone;

private:

    void init();
    void Start();
    void Update();
    void Draw();
    //init
    void initTextures();
    void initMesh();
    void initShaders();
    void initGaussianRandom();
    void initClock();

    //compute
    void ComputeFFT(std::shared_ptr<Shader> shader, std::shared_ptr<ImageTexture>input_Texture);
    void ComputeOceanValue();
};