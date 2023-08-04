#include<glad/glad.h>
#include<glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>
#include <vector>

#include"component/Ocean.h"
#include"buffer/ImageTexture.h"
#include"renderer/Texture.h"
#include"system/RenderManager.h"
#include"renderer/Renderpass.h"
#include"renderer/RenderScene.h"
#include"object/SkyBox.h"
#include"component/Atmosphere.h"
//#include"buffer/UniformBuffer.h"
#include"utils/Shader.h"
#include"utils/Utils.h"
#include<utility>
#include<random>
#include<time.h>

extern std::shared_ptr<RenderScene> scene;

Ocean::Ocean()
{
    Component::name = "Ocean";
    initDone = false;

    // texture
    GaussianRandomRT_Texture=nullptr;
    HeightSpectrumRT_Texture=nullptr;
    DisplaceXSpectrumRT_Texture=nullptr;
    DisplaceZSpectrumRT_Texture=nullptr;
    InputRT_Texture=nullptr;
    OutputRT_Texture=nullptr;
    DisplaceRT_Texture=nullptr;
    NormalRT_Texture=nullptr;
    BubblesRT_Texture=nullptr;

    //shaders
    GaussianRandomRT_Shader=nullptr;
    DisplaceSpectrum_Shader=nullptr;
    HeightSpectrum_Shader=nullptr;
    FFTHorizontal_Shader=nullptr;
    FFTHorizontalEnd_Shader=nullptr;
    FFTVertical_Shader=nullptr;
    FFTVerticalEnd_Shader=nullptr;
    TextureDisplace_Shader=nullptr;
    TextureNormalBubbles_Shader=nullptr;
    
    draw_shader=nullptr;
}
Ocean::~Ocean()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteVertexArrays(1, &texture_VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);  
}
void Ocean::init()
{
    name = "Ocean";
    //prepare all data
    this->Start();
}
void Ocean::render()
{
    if (!initDone)
    {
        init();
        initDone = true;
    }

    this->Update();
    this->Draw();
}

//origin
void Ocean::initTextures()
{
    // initTextures
        //创建对象
    GaussianRandomRT_Texture = std::make_shared<ImageTexture>();//高斯随机数
    HeightSpectrumRT_Texture = std::make_shared<ImageTexture>();//高度频谱
    DisplaceXSpectrumRT_Texture = std::make_shared<ImageTexture>();//X偏移频谱
    DisplaceZSpectrumRT_Texture = std::make_shared<ImageTexture>();//Z偏移频谱
    OutputRT_Texture = std::make_shared<ImageTexture>();
    DisplaceRT_Texture = std::make_shared<ImageTexture>();
    NormalRT_Texture = std::make_shared<ImageTexture>();
    BubblesRT_Texture = std::make_shared<ImageTexture>();

    //生成对应空间
    GaussianRandomRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    HeightSpectrumRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    DisplaceXSpectrumRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    DisplaceZSpectrumRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    OutputRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    DisplaceRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    NormalRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    BubblesRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);

    //绑定
    //GaussianRandomRT_Texture->setBinding(1);
 //   HeightSpectrumRT_Texture->setBinding(2);
 //   DisplaceXSpectrumRT_Texture->setBinding(3);
 //   DisplaceZSpectrumRT_Texture->setBinding(4);
 //   InputRT_Texture->setBinding(5);
 //   OutputRT_Texture->setBinding(6);
 //   DisplaceRT_Texture->setBinding(7);
 //   NormalRT_Texture->setBinding(8);
 //   BubblesRT_Texture->setBinding(9);
}

void Ocean::initShaders()
{
    // initShaders
    //new shader
    GaussianRandomRT_Shader = std::make_shared<Shader>("./src/shader/ocean/ocean_ComputeGaussianRandom.comp");
    DisplaceSpectrum_Shader = std::make_shared<Shader>("./src/shader/ocean/ocean_CreateDisplaceSpectrum.comp");
    HeightSpectrum_Shader = std::make_shared<Shader>("./src/shader/ocean/ocean_CreateHeightSpectrum.comp");
    FFTHorizontal_Shader = std::make_shared<Shader>("./src/shader/ocean/ocean_FFTHorizontal.comp");
    FFTHorizontalEnd_Shader = std::make_shared<Shader>("./src/shader/ocean/ocean_FFTHorizontalEnd.comp");
    FFTVertical_Shader = std::make_shared<Shader>("./src/shader/ocean/ocean_FFTVertical.comp");
    FFTVerticalEnd_Shader = std::make_shared<Shader>("./src/shader/ocean/ocean_FFTVerticalEnd.comp");
    TextureDisplace_Shader = std::make_shared<Shader>("./src/shader/ocean/ocean_TextureGenerationDisplace.comp");
    TextureNormalBubbles_Shader = std::make_shared<Shader>("./src/shader/ocean/ocean_TextureGenerationNormalBubbles.comp");

    draw_shader = std::make_shared<Shader>("./src/shader/ocean/ocean.vs", "./src/shader/ocean/ocean.fs");
}

void Ocean::initGaussianRandom()
{
    GaussianRandomRT_Shader->use();
    GaussianRandomRT_Shader->setInt("N", fft_size);//fft纹理大小

    GaussianRandomRT_Texture->setBinding(1);
    glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Ocean::initClock()
{
    srand((unsigned int)time(NULL));
}

void Ocean::initMesh()//VAO,VBO,EBO
{
    //fftSize = (int)Mathf.Pow(2, FFTPow);

    //生成指定大小空间
    vertexInfo.resize(5 * MeshSize * MeshSize);
    //positions.resize(MeshSize * MeshSize);
    vertexIndexs.resize((MeshSize - 1) * (MeshSize - 1) * 6);
    //uvs.resize(MeshSize * MeshSize);

    int inx = 0;
    for (int i = 0; i < MeshSize; i++)
    {
        for (int j = 0; j < MeshSize; j++)
        {
            int index = i * MeshSize + j;
            vertexInfo[5 * index] = (j - MeshSize / 2.0f) * MeshLength / MeshSize;
            vertexInfo[5 * index + 1] = 0;
            vertexInfo[5 * index + 2] = (i - MeshSize / 2.0f) * MeshLength / MeshSize;
            vertexInfo[5 * index + 3] = j / (MeshSize - 1.0f);
            vertexInfo[5 * index + 4] = i / (MeshSize - 1.0f);

            //positions[index] = glm::vec3((j - MeshSize / 2.0f) * MeshLength / MeshSize, 0, (i - MeshSize / 2.0f) * MeshLength / MeshSize);//Vertex Pos
            //uvs[index] = glm::vec2(j / (MeshSize - 1.0f), i / (MeshSize - 1.0f));//texture coordinates

            if (i != MeshSize - 1 && j != MeshSize - 1)
            {
                //EBO
                // vertexIndexs.push_back(index);
                // vertexIndexs.push_back(index + MeshSize);
                // vertexIndexs.push_back(index + MeshSize+1);

                // vertexIndexs.push_back(index);
                // vertexIndexs.push_back(index + MeshSize + 1);
                // vertexIndexs.push_back(index+1);

                vertexIndexs[inx++] = index;
                vertexIndexs[inx++] = index + MeshSize;
                vertexIndexs[inx++] = index + MeshSize + 1;

                vertexIndexs[inx++] = index;
                vertexIndexs[inx++] = index + MeshSize + 1;
                vertexIndexs[inx++] = index + 1;
            }
        }
    }

    //binding
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);//VAO会自动记录相应EBO信息
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexInfo.size() * sizeof(float), &vertexInfo[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, vertexIndexs.size() * sizeof(unsigned int), &vertexIndexs[0], GL_STATIC_DRAW);


    // 设置顶点属性指针(这样VAO才设置结束）
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);//pos
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));//uv
    //最后一个指定起始地址，第一个指定对应location = i的数据如何从VBO中读取（VAO装入VBO信息）
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);


}

void Ocean::ComputeFFT(std::shared_ptr<Shader> shader, std::shared_ptr<ImageTexture>input_Texture)//单行/列求和
{
    //shader->use();

    //设置需要使用的数据给"InputRT"
    input_Texture->setBinding(5);//binding=5的区域指向了对应input_Texture的数据区
    OutputRT_Texture->setBinding(6);//指向Output_Texture空间

    glDispatchCompute(fft_size / 8, fft_size / 8, 1);//调用FFTHorizontal_Shader对应的comp
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    //目前计算结果存在OutputRT_Texture里（并行算出所有的这一层的输出）

    //交换输入输出区——使得for循环里这一层的输出再变成下一层的输入->不断累加
    std::swap(input_Texture->tex->id, OutputRT_Texture->tex->id);
}

//计算海洋数据
void Ocean::ComputeOceanValue()
{
    //uniform赋值
    //1、GaussianRandomRT_Shader——only once
    //GaussianRandomRT_Shader->use();
    //GaussianRandomRT_Shader->setInt("N", fft_size);//fft纹理大小

    //GaussianRandomRT_Texture->setBinding(1);
    //glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    //glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    //2、HeightSpectrum_Shader
    WindAndSeed.z = (10 + rand() % 90) / (10.0f);//Random.Range(1,10.0f)
    WindAndSeed.w = (10 + rand() % 90) / (10.0f);//Random.Range(1,10.0f)
    glm::vec2 wind = glm::vec2(WindAndSeed.x, WindAndSeed.y);
    wind = normalize(wind);
    wind *= WindScale;
    HeightSpectrum_Shader->use();
    HeightSpectrum_Shader->setInt("N", fft_size);
    HeightSpectrum_Shader->setFloat("Time", inner_time);// inner_time
    HeightSpectrum_Shader->setVec4("WindAndSeed", glm::vec4(wind.x, wind.y, WindAndSeed.z, WindAndSeed.w));
    HeightSpectrum_Shader->setFloat("A", A);

    GaussianRandomRT_Texture->setBinding(1);
    HeightSpectrumRT_Texture->setBinding(2);
    glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);


    //3、DisplaceSpectrum_Shader
    DisplaceSpectrum_Shader->use();
    DisplaceSpectrum_Shader->setInt("N", fft_size);//fft纹理大小

    HeightSpectrumRT_Texture->setBinding(2);
    DisplaceXSpectrumRT_Texture->setBinding(3);
    DisplaceZSpectrumRT_Texture->setBinding(4);
    glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    //4、FFT
    //FFT部分 for循环

    //横向FFT
    for (int m = 1; m <= FFTPow; m++)
    {
        int ns = int(pow(2, m - 1));
        //最后一次特殊处理
        if (m != FFTPow)
        {
            FFTHorizontal_Shader->use();
            FFTHorizontal_Shader->setInt("N", fft_size);
            FFTHorizontal_Shader->setInt("Ns", ns);//Ns = pow(2,m-1); m为第几阶段
            ComputeFFT(FFTHorizontal_Shader, HeightSpectrumRT_Texture);//结果又在HeightSpectrumRT_Texture中
            ComputeFFT(FFTHorizontal_Shader, DisplaceXSpectrumRT_Texture);
            ComputeFFT(FFTHorizontal_Shader, DisplaceZSpectrumRT_Texture);
        }
        else
        {
            FFTHorizontalEnd_Shader->use();
            FFTHorizontalEnd_Shader->setInt("N", fft_size);
            FFTHorizontalEnd_Shader->setInt("Ns", ns);
            ComputeFFT(FFTHorizontalEnd_Shader, HeightSpectrumRT_Texture);
            ComputeFFT(FFTHorizontalEnd_Shader, DisplaceXSpectrumRT_Texture);
            ComputeFFT(FFTHorizontalEnd_Shader, DisplaceZSpectrumRT_Texture);
        }

    }
    //纵向FFT
    for (int m = 1; m <= FFTPow; m++)
    {
        int ns = int(pow(2, m - 1));

        //最后一次特殊处理
        if (m != FFTPow)
        {
            FFTVertical_Shader->use();
            FFTVertical_Shader->setInt("N", fft_size);
            FFTVertical_Shader->setInt("Ns", ns);
            ComputeFFT(FFTVertical_Shader, HeightSpectrumRT_Texture);//结果又在HeightSpectrumRT_Texture中
            ComputeFFT(FFTVertical_Shader, DisplaceXSpectrumRT_Texture);
            ComputeFFT(FFTVertical_Shader, DisplaceZSpectrumRT_Texture);
        }
        else
        {
            FFTVerticalEnd_Shader->use();
            FFTVerticalEnd_Shader->setInt("N", fft_size);
            FFTVerticalEnd_Shader->setInt("Ns", ns);
            ComputeFFT(FFTVerticalEnd_Shader, HeightSpectrumRT_Texture);
            ComputeFFT(FFTVerticalEnd_Shader, DisplaceXSpectrumRT_Texture);
            ComputeFFT(FFTVerticalEnd_Shader, DisplaceZSpectrumRT_Texture);
        }

    }


    //此时已经算出XYZ的偏移

    //5、TextureDisplace_Shader
    TextureDisplace_Shader->use();
    TextureDisplace_Shader->setInt("N", fft_size);
    TextureDisplace_Shader->setFloat("Lambda", Lambda);
    TextureDisplace_Shader->setFloat("HeightScale", HeightScale);

    //计算纹理偏移——读偏移量x,y,z从三张图里，写到DisplaceRT
    HeightSpectrumRT_Texture->setBinding(2);
    DisplaceXSpectrumRT_Texture->setBinding(3);
    DisplaceZSpectrumRT_Texture->setBinding(4);
    DisplaceRT_Texture->setBinding(7);
    glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);


    //6、TextureNormalBubbles_Shader
    //计算法线和泡沫纹理
    TextureNormalBubbles_Shader->use();
    TextureNormalBubbles_Shader->setInt("N", fft_size);
    TextureNormalBubbles_Shader->setFloat("OceanLength", MeshLength);
    TextureNormalBubbles_Shader->setFloat("BubblesScale", BubblesScale);
    TextureNormalBubbles_Shader->setFloat("BubblesThreshold", BubblesThreshold);

    DisplaceRT_Texture->setBinding(7);
    NormalRT_Texture->setBinding(5);//new 覆盖
    BubblesRT_Texture->setBinding(6);//new 覆盖
    glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}


//总控程序
void Ocean::Start()
{
    //创建mesh
    initMesh();
    
    //初始化ComputerShader相关数据
    initTextures();
    
    initShaders();
    
    //计算GaussianRandom

    //初始化随机数
    initClock();
    
    initGaussianRandom();
    
}

void Ocean::Update()
{
    //inner_time += this->deltaTime * TimeScale;
    inner_time=static_cast<float>(glfwGetTime() * TimeScale);

    //计算海洋数据
    ComputeOceanValue();
}

void Ocean::Draw()
{
    draw_shader->use();


    //lighting
    //draw_shader->setVec3("material.ka", 2.0f, 2.0f, 2.0f);
    //draw_shader->setVec3("material.kd", 0.5f, 0.5f, 0.5f);
    //draw_shader->setVec3("material.ks", 0.5f, 0.5f, 0.5f);
    //draw_shader->setFloat("material.shininess", 32.0f);
    
    //const glm::vec3 camera_Position = glm::vec3(0.0f, 1.0f, 0.0f);
    //draw_shader->setVec3("viewPos", camera_Position);

    draw_shader->setVec3("dirLight.direction", 0.0f, -1.0f, 0.0f);
    draw_shader->setVec3("dirLight.ambient", 0.2f, 0.2f, 0.2f);
    draw_shader->setVec3("dirLight.diffuse", 1.0f, 1.0f, 1.0f);
    draw_shader->setVec3("dirLight.specular", 0.5f, 0.5f, 0.5f);

    draw_shader->setFloat("outer_FresnelScale", outer_FresnelScale);
    draw_shader->setVec3("outer_OceanColorShallow", outer_OceanColorShallow);
    draw_shader->setVec3("outer_OceanColorDeep", outer_OceanColorDeep);
    draw_shader->setVec3("outer_BubblesColor", outer_BubblesColor);
    draw_shader->setVec3("outer_Specular", outer_Specular);
    draw_shader->setInt("outer_Gloss", outer_Gloss);
    draw_shader->setVec3("outer_ambient", outer_ambient);

    //texture
    glActiveTexture(GL_TEXTURE0); // 在绑定纹理之前先激活纹理单元0
    glBindTexture(GL_TEXTURE_2D, DisplaceRT_Texture->tex->id);
    draw_shader->setInt("DisplaceRT", 0);//set up textures——对应激活位置

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, NormalRT_Texture->tex->id);
    draw_shader->setInt("NormalRT", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, BubblesRT_Texture->tex->id);
    draw_shader->setInt("BubblesRT", 2);//此时通过名字查找，而非binding号

    // 绑定之前的图像和深度
    glActiveTexture(GL_TEXTURE3);
    if (scene->sky) {
        auto&& atmos = std::static_pointer_cast<Atmosphere>(scene->sky->GetComponent("Atmosphere"));
        if (atmos) {
            glBindTexture(GL_TEXTURE_2D, atmos->skyViewTexture->tex->id);
        }
    }
    draw_shader->setInt("skyview", 3);

    //MVP
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(0.0f,-5.0f,0.0f));
    
    draw_shader->setMat4("model", model);
    //draw_shader->setMat4("view", view);
    //draw_shader->setMat4("projection", projection);

    //draw
    draw_shader->setFloat("OceanLength", MeshLength);


    // glEnable(GL_CULL_FACE);
    // glCullFace(GL_BACK);
    //glDisable(GL_CULL_FACE);

    glBindVertexArray(VAO);
    //glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);

    // 此处需要开启混合功能
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawElements(GL_TRIANGLES, (MeshSize - 1) * (MeshSize - 1) * 6, GL_UNSIGNED_INT, 0);
    glDisable(GL_BLEND);
}
