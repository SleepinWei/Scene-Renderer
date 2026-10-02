#pragma once
#include <cstddef>
#include <vector>
struct GLFWwindow;
struct ImDrawData;
namespace MetalBackend {
void initialize(GLFWwindow* window, int width, int height);
void beginFrame();
void present();
void shutdown();
void resize(int width, int height);
void guiInitialize();
void guiNewFrame();
void guiRender(ImDrawData* data);
void guiShutdown();
unsigned createProgram(const char* vertex, const char* fragment,
                       const char* geometry, const char* control, const char* evaluation);
unsigned createComputeProgram(const char* path);
void setUniform(unsigned program, const char* name, const void* data,
                unsigned components, unsigned columns = 1);
void useProgram(unsigned program);
void setBlockBinding(unsigned program, const char* name, unsigned binding);
void selfTest();
void capture(const char* path);
struct FloatTexture { unsigned width, height; std::vector<float> rgba; };
FloatTexture readFloatTexture(unsigned texture);
void beginGPUCapture(const char* path);
void endGPUCapture();
void inspectTexture(unsigned texture,const char* path);
}
