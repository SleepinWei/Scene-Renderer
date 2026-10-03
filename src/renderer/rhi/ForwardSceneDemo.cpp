#include "renderer/rhi/SceneAdapter.h"
#include "rhi/ShaderAssets.h"
#include "renderer/RenderScene.h"
#include "renderer/Material.h"
#include "component/GameObject.h"
#include "component/Mesh_Filter.h"
#include "component/transform.h"
#include "component/Lights.h"
#include "component/Ocean.h"
#include "component/Atmosphere.h"
#include "object/SkyBox.h"
#include "utils/Camera.h"
#include "utils/Utils.h"
#include <glfw/glfw3.h>
#include <fstream>
#include <iostream>
namespace render {
std::shared_ptr<RenderScene> makeForwardDemoScene() {
    auto scene = std::make_shared<RenderScene>();scene->setCamera(std::make_shared<Camera>(glm::vec3(0,2,8),glm::vec3(0,1,0),-90,-9));scene->mainCamera()->exposure = 1;
    const glm::vec3 colors[] = {{.8f,.18f,.12f},{.18f,.65f,.95f},{.85f,.63f,.2f}};
    for (int i = 0; i < 3; ++i) {
        auto object = std::make_shared<GameObject>();object->name = "RHI sphere " + std::to_string(i);
        auto transform = object->addComponent<Transform>();transform->position = {float(i-1)*2.4f,0,0};
        auto filter = object->addComponent<MeshFilter>();auto mesh = Mesh::initSphere(48);mesh->material = std::make_shared<Material>();
        mesh->material->albedoFactor = colors[i];mesh->material->metallicFactor = i == 2 ? 1.f : 0.f;mesh->material->roughnessFactor = i == 2 ? .25f : .55f;
        filter->addMesh(mesh);scene->addObject(object);
    }
    auto floor = std::make_shared<GameObject>();floor->name = "RHI floor";auto transform = floor->addComponent<Transform>();transform->position.y = -1.1f;transform->scale = {5,1,5};
    auto mesh = Mesh::initPlane();mesh->material = std::make_shared<Material>();mesh->material->albedoFactor = {.42f,.44f,.48f};
    floor->addComponent<MeshFilter>()->addMesh(mesh);scene->addObject(floor);
    auto sun = std::make_shared<GameObject>();sun->addComponent<Transform>();auto light = sun->addComponent<DirectionLight>();light->data.color = {4,3.8f,3.6f};light->data.direction = {-.4f,-.8f,-1};scene->addObject(sun);
    return scene;
}
void runForwardScene(int argc, char** argv) {
    int frames = 0;bool hidden = false, vulkan = rhi::requestedBackend()==rhi::Backend::Vulkan,withOcean=false,withSky=false;std::string screenshot;float fixedTime=-1;
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--frames" && i+1 < argc) { frames = std::stoi(argv[++i]);if (frames <= 0) throw std::invalid_argument("frames must be positive"); }
        else if(argument=="--ocean"){withOcean=true;withSky=true;}
        else if(argument=="--time" && i+1<argc)fixedTime=std::stof(argv[++i]);
        else if(argument=="--sky")withSky=true;
        else if (argument == "--hidden") hidden = true;
        else if (argument == "--backend" && i+1<argc) { const std::string name=argv[++i];if(name!="Vulkan" && name!="Metal" && name!="OpenGL")throw std::invalid_argument("Unknown backend");vulkan=name=="Vulkan"; }
        else if (argument == "--screenshot" && i+1 < argc) screenshot = argv[++i];
        else throw std::invalid_argument("Unknown RHI forward argument: " + argument);
    }
    if (hidden && frames == 0) throw std::invalid_argument("hidden forward rendering needs --frames");
#ifdef SCENERENDERER_HAS_VULKAN
    if(vulkan)rhi::configureVulkanWindowing();
#endif
    if (!glfwInit()) throw std::runtime_error("Forward scene: GLFW initialization failed");
    glfwWindowHint(GLFW_VISIBLE,hidden ? GLFW_FALSE : GLFW_TRUE);GLFWwindow* window = nullptr;
    try {
        if(vulkan) {
#ifdef SCENERENDERER_HAS_VULKAN
            glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
#ifdef __APPLE__
            glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER,GLFW_FALSE);
#endif
            window=glfwCreateWindow(800,600,"SceneRenderer Vulkan",nullptr,nullptr);if(!window)throw std::runtime_error("Vulkan scene: window creation failed");
            rhi::installDevice(rhi::makeVulkanDevice(window));
#else
            throw std::invalid_argument("Vulkan backend was not enabled in this build");
#endif
        } else {
            if (createWindow(window,800,600) != 0) throw std::runtime_error("Forward scene: window creation failed");
            if (gladInit() != 0) throw std::runtime_error("Forward scene: GLAD initialization failed");
        }
        auto device = rhi::graphicsDevice();auto scene = makeForwardDemoScene();
        if(withSky){scene->addSky(std::make_shared<Sky>());scene->sky()->addComponent<Atmosphere>()->sunAngle=20;}
        if(withOcean){auto object=std::make_shared<GameObject>();auto ocean=object->addComponent<Ocean>();ocean->fft_size=256;ocean->MeshSize=129;ocean->MeshLength=32;ocean->seaLevel=-.5f;scene->addObject(object);}
        {
            SceneAdapter adapter(device);int width, height;glfwGetFramebufferSize(window,&width,&height);
            ForwardPbrRenderer renderer(device,rhi::defaultShaderDirectory(),width,height, std::string(argv[1]) == "--rhi-scene" ? PbrPath::Scene : std::string(argv[1]) == "--rhi-deferred" ? PbrPath::Deferred : PbrPath::Forward);int rendered = 0;
            while (!glfwWindowShouldClose(window)) {
                glfwPollEvents();glfwGetFramebufferSize(window,&width,&height);
                if (width <= 0 || height <= 0) { glfwWaitEventsTimeout(.05);continue; }
                if (glfwGetKey(window,GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
                scene->mainCamera()->aspect_ratio = float(width)/height;
                device->beginFrame();renderer.resize(width,height);auto frame = adapter.collect(scene,fixedTime);if(std::string(argv[1])!="--rhi-scene"){frame.frame.sky=false;frame.frame.taa=false;frame.frame.oceans.clear();}if(fixedTime>=0)frame.frame.timeSeconds=fixedTime;renderer.render(frame.frame,frame.packets,frame.exposure);
                device->copyToBackbuffer(renderer.output());device->present();++rendered;
                if (frames > 0 && rendered >= frames) break;
            }
            if (!screenshot.empty()) {
                const auto pixels = renderer.readOutput();std::ofstream output(screenshot,std::ios::binary);output << "P6\n" << width << ' ' << height << "\n255\n";
                for (size_t i = 0; i < pixels.size(); i += 4) output.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                if (!output) throw std::runtime_error("Forward scene: cannot save screenshot");
            }
            std::cout << "RHI forward scene rendered " << rendered << " frames\n";
        }
        scene->destroy();scene.reset();rhi::shutdown();glfwDestroyWindow(window);glfwTerminate();
    } catch (...) { try { rhi::shutdown(); } catch (...) {}if (window) glfwDestroyWindow(window);glfwTerminate();throw; }
}
}
