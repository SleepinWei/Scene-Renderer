#include "rhi/GraphicsDevice.h"
#include "rhi/Validation.h"
#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int main() {
    GLFWwindow* window = nullptr;
    try {
        check(glfwInit() != 0, "GLFW initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);window = glfwCreateWindow(64, 64, "RHI GL state validation", nullptr, nullptr);
        check(window != nullptr, "OpenGL context creation failed");glfwMakeContextCurrent(window);
        check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0, "GLAD initialization failed");
        auto d = std::dynamic_pointer_cast<rhi::GraphicsDevice>(rhi::makeOpenGLDevice(window));
        GLuint buffers[3], texture, sampler, vao;glGenBuffers(3, buffers);glGenTextures(1, &texture);glGenSamplers(1, &sampler);glGenVertexArrays(1, &vao);
        glBindBuffer(GL_UNIFORM_BUFFER, buffers[0]);glBufferData(GL_UNIFORM_BUFFER, 512, nullptr, GL_STATIC_DRAW);
        glBindBufferRange(GL_UNIFORM_BUFFER, 3, buffers[0], 256, 16);glBindBuffer(GL_UNIFORM_BUFFER, buffers[1]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffers[2]);glBufferData(GL_PIXEL_UNPACK_BUFFER, 512, nullptr, GL_STATIC_DRAW);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, buffers[2]);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER, buffers[1]);
        glActiveTexture(GL_TEXTURE11);glBindTexture(GL_TEXTURE_2D, texture);glBindSampler(11, sampler);
        const GLenum stores[] = {GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS};
        const GLint values[] = {128, 7, 3, 128, 5, 2};for (unsigned i = 0; i < 6; ++i) glPixelStorei(stores[i], values[i]);
        glPixelStorei(GL_PACK_ALIGNMENT, 8);glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        const GLenum capabilities[] = {GL_STENCIL_TEST, GL_DEPTH_CLAMP, GL_POLYGON_OFFSET_FILL, GL_COLOR_LOGIC_OP,
            GL_RASTERIZER_DISCARD, GL_SCISSOR_TEST, GL_FRAMEBUFFER_SRGB, GL_CLIP_DISTANCE0, GL_PRIMITIVE_RESTART};
        for (auto capability : capabilities) glEnable(capability);
        glViewport(3, 4, 17, 19);glDepthRange(.2, .7);glDepthMask(GL_FALSE);glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glColorMaski(1,GL_TRUE,GL_FALSE,GL_TRUE,GL_FALSE);glEnablei(GL_BLEND,1);
        glBlendFuncSeparatei(1,GL_ONE,GL_ZERO,GL_ZERO,GL_ONE);glBlendEquationSeparatei(1,GL_MAX,GL_MIN);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER,buffers[1]);
        check(glGetError() == GL_NO_ERROR, "invalid legacy GL test setup");
        rhi::validateTexturedRendering(*d);rhi::validateComputeAndIndirect(*d);
        auto integer = [](GLenum name) { GLint result = 0;glGetIntegerv(name, &result);return result; };
        check(integer(GL_UNIFORM_BUFFER_BINDING) == GLint(buffers[1]), "generic uniform binding changed");
        GLint slot = 0;glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 3, &slot);check(slot == GLint(buffers[0]), "uniform slot changed");
        GLint64 offset = 0;glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 3, &offset);check(offset == 256, "uniform offset changed");
        check(integer(GL_PIXEL_PACK_BUFFER_BINDING) == GLint(buffers[2]) && integer(GL_PIXEL_UNPACK_BUFFER_BINDING) == GLint(buffers[2]), "pixel buffer bindings changed");
        for (unsigned i = 0; i < 6; ++i) check(integer(stores[i]) == values[i], "pixel store state changed");
        check(integer(GL_PACK_ALIGNMENT) == 8 && integer(GL_UNPACK_ALIGNMENT) == 8, "pixel alignment changed");
        check(integer(GL_ACTIVE_TEXTURE) == GL_TEXTURE11 && integer(GL_TEXTURE_BINDING_2D) == GLint(texture), "texture state changed");
        glGetIntegerv(GL_SAMPLER_BINDING, &slot);check(slot == GLint(sampler), "sampler state changed");
        check(integer(GL_VERTEX_ARRAY_BINDING) == GLint(vao) && integer(GL_ARRAY_BUFFER_BINDING) == GLint(buffers[1]), "vertex binding changed");
        for (auto capability : capabilities) check(glIsEnabled(capability), "legacy enable state changed");
        GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT, viewport);check(viewport[0] == 3 && viewport[1] == 4 && viewport[2] == 17 && viewport[3] == 19, "viewport changed");
        GLdouble range[2]{};glGetDoublev(GL_DEPTH_RANGE, range);check(range[0] > .19 && range[0] < .21 && range[1] > .69 && range[1] < .71, "depth range changed");
        GLboolean mask[4]{};glGetBooleanv(GL_COLOR_WRITEMASK, mask);check(!mask[0] && mask[1] && !mask[2] && mask[3], "color mask changed");
        glGetBooleanv(GL_DEPTH_WRITEMASK, mask);check(!mask[0], "depth mask changed");
        check(integer(GL_BLEND_EQUATION_RGB) == GL_FUNC_REVERSE_SUBTRACT && integer(GL_BLEND_EQUATION_ALPHA) == GL_FUNC_SUBTRACT, "blend equation changed");
        GLint polygon[2]{};glGetIntegerv(GL_POLYGON_MODE, polygon);check(polygon[0] == GL_LINE, "polygon mode changed");
        glGetBooleani_v(GL_COLOR_WRITEMASK,1,mask);check(mask[0] && !mask[1] && mask[2] && !mask[3],"indexed color mask changed");
        glGetIntegeri_v(GL_BLEND_EQUATION_RGB,1,&slot);check(slot==GL_MAX && glIsEnabledi(GL_BLEND,1),"indexed blend state changed");
        glGetIntegeri_v(GL_BLEND_SRC_RGB,1,&slot);check(slot==GL_ONE,"indexed blend function changed");
        check(integer(GL_DRAW_INDIRECT_BUFFER_BINDING)==GLint(buffers[1]),"indirect binding changed");
        check(glGetError() == GL_NO_ERROR, "RHI left a GL error");
        d->close();glDeleteBuffers(3, buffers);glDeleteTextures(1, &texture);glDeleteSamplers(1, &sampler);glDeleteVertexArrays(1, &vao);
        glfwDestroyWindow(window);glfwTerminate();std::cout << "RHI preserved legacy OpenGL state and ignored conflicting state during drawing\n";return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n';if (window) glfwDestroyWindow(window);glfwTerminate();return 1; }
}
