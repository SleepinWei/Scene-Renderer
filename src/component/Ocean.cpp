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
        //��������
    GaussianRandomRT_Texture = std::make_shared<ImageTexture>();//��˹�����
    HeightSpectrumRT_Texture = std::make_shared<ImageTexture>();//�߶�Ƶ��
    DisplaceXSpectrumRT_Texture = std::make_shared<ImageTexture>();//Xƫ��Ƶ��
    DisplaceZSpectrumRT_Texture = std::make_shared<ImageTexture>();//Zƫ��Ƶ��
    OutputRT_Texture = std::make_shared<ImageTexture>();
    DisplaceRT_Texture = std::make_shared<ImageTexture>();
    NormalRT_Texture = std::make_shared<ImageTexture>();
    BubblesRT_Texture = std::make_shared<ImageTexture>();

    //���ɶ�Ӧ�ռ�
    GaussianRandomRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    HeightSpectrumRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    DisplaceXSpectrumRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    DisplaceZSpectrumRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    OutputRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    DisplaceRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    NormalRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);
    BubblesRT_Texture->genImageTexture(GL_RGBA32F, GL_RGBA, fft_size, fft_size);

    //��
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
    GaussianRandomRT_Shader->setInt("N", fft_size);//fft�����С

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

    //����ָ����С�ռ�
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

    glBindVertexArray(VAO);//VAO���Զ���¼��ӦEBO��Ϣ
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexInfo.size() * sizeof(float), &vertexInfo[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, vertexIndexs.size() * sizeof(unsigned int), &vertexIndexs[0], GL_STATIC_DRAW);


    // ���ö�������ָ��(����VAO�����ý�����
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);//pos
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));//uv
    //���һ��ָ����ʼ��ַ����һ��ָ����Ӧlocation = i��������δ�VBO�ж�ȡ��VAOװ��VBO��Ϣ��
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);


}

void Ocean::ComputeFFT(std::shared_ptr<Shader> shader, std::shared_ptr<ImageTexture>input_Texture)//����/�����
{
    //shader->use();

    //������Ҫʹ�õ����ݸ�"InputRT"
    input_Texture->setBinding(5);//binding=5������ָ���˶�Ӧinput_Texture��������
    OutputRT_Texture->setBinding(6);//ָ��Output_Texture�ռ�

    glDispatchCompute(fft_size / 8, fft_size / 8, 1);//����FFTHorizontal_Shader��Ӧ��comp
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    //Ŀǰ����������OutputRT_Texture�����������е���һ��������

    //�����������������ʹ��forѭ������һ�������ٱ����һ�������->�����ۼ�
    std::swap(input_Texture->tex->id, OutputRT_Texture->tex->id);
}

//���㺣������
void Ocean::ComputeOceanValue()
{
    //uniform��ֵ
    //1��GaussianRandomRT_Shader����only once
    //GaussianRandomRT_Shader->use();
    //GaussianRandomRT_Shader->setInt("N", fft_size);//fft�����С

    //GaussianRandomRT_Texture->setBinding(1);
    //glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    //glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    //2��HeightSpectrum_Shader
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


    //3��DisplaceSpectrum_Shader
    DisplaceSpectrum_Shader->use();
    DisplaceSpectrum_Shader->setInt("N", fft_size);//fft�����С

    HeightSpectrumRT_Texture->setBinding(2);
    DisplaceXSpectrumRT_Texture->setBinding(3);
    DisplaceZSpectrumRT_Texture->setBinding(4);
    glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    //4��FFT
    //FFT���� forѭ��

    //����FFT
    for (int m = 1; m <= FFTPow; m++)
    {
        int ns = int(pow(2, m - 1));
        //���һ�����⴦��
        if (m != FFTPow)
        {
            FFTHorizontal_Shader->use();
            FFTHorizontal_Shader->setInt("N", fft_size);
            FFTHorizontal_Shader->setInt("Ns", ns);//Ns = pow(2,m-1); mΪ�ڼ��׶�
            ComputeFFT(FFTHorizontal_Shader, HeightSpectrumRT_Texture);//�������HeightSpectrumRT_Texture��
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
    //����FFT
    for (int m = 1; m <= FFTPow; m++)
    {
        int ns = int(pow(2, m - 1));

        //���һ�����⴦��
        if (m != FFTPow)
        {
            FFTVertical_Shader->use();
            FFTVertical_Shader->setInt("N", fft_size);
            FFTVertical_Shader->setInt("Ns", ns);
            ComputeFFT(FFTVertical_Shader, HeightSpectrumRT_Texture);//�������HeightSpectrumRT_Texture��
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


    //��ʱ�Ѿ����XYZ��ƫ��

    //5��TextureDisplace_Shader
    TextureDisplace_Shader->use();
    TextureDisplace_Shader->setInt("N", fft_size);
    TextureDisplace_Shader->setFloat("Lambda", Lambda);
    TextureDisplace_Shader->setFloat("HeightScale", HeightScale);

    //��������ƫ�ơ�����ƫ����x,y,z������ͼ�д��DisplaceRT
    HeightSpectrumRT_Texture->setBinding(2);
    DisplaceXSpectrumRT_Texture->setBinding(3);
    DisplaceZSpectrumRT_Texture->setBinding(4);
    DisplaceRT_Texture->setBinding(7);
    glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);


    //6��TextureNormalBubbles_Shader
    //���㷨�ߺ���ĭ����
    TextureNormalBubbles_Shader->use();
    TextureNormalBubbles_Shader->setInt("N", fft_size);
    TextureNormalBubbles_Shader->setFloat("OceanLength", MeshLength);
    TextureNormalBubbles_Shader->setFloat("BubblesScale", BubblesScale);
    TextureNormalBubbles_Shader->setFloat("BubblesThreshold", BubblesThreshold);

    DisplaceRT_Texture->setBinding(7);
    NormalRT_Texture->setBinding(5);//new ����
    BubblesRT_Texture->setBinding(6);//new ����
    glDispatchCompute(fft_size / 8, fft_size / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}


//�ܿس���
void Ocean::Start()
{
    //����mesh
    initMesh();
    
    //��ʼ��ComputerShader�������
    initTextures();
    
    initShaders();
    
    //����GaussianRandom

    //��ʼ�������
    initClock();
    
    initGaussianRandom();
    
}

void Ocean::Update()
{
    //inner_time += this->deltaTime * TimeScale;
    inner_time=static_cast<float>(glfwGetTime() * TimeScale);

    //���㺣������
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
    glActiveTexture(GL_TEXTURE0); // �ڰ�����֮ǰ�ȼ�������Ԫ0
    glBindTexture(GL_TEXTURE_2D, DisplaceRT_Texture->tex->id);
    draw_shader->setInt("DisplaceRT", 0);//set up textures������Ӧ����λ��

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, NormalRT_Texture->tex->id);
    draw_shader->setInt("NormalRT", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, BubblesRT_Texture->tex->id);
    draw_shader->setInt("BubblesRT", 2);//��ʱͨ�����ֲ��ң�����binding��

    // ��֮ǰ��ͼ������
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

    // �˴���Ҫ������Ϲ���
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawElements(GL_TRIANGLES, (MeshSize - 1) * (MeshSize - 1) * 6, GL_UNSIGNED_INT, 0);
    glDisable(GL_BLEND);
}
