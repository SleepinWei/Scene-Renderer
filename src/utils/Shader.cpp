#include<glad/glad.h>
#include"utils/Shader.h"
#include<glfw/glfw3.h>
#include<regex>
#include<vector>

namespace {
struct UniformBlockBinding {
    std::string name;
    GLuint binding;
};

// macOS system OpenGL is 4.1 core (GLSL 410). The Windows shaders target
// 4.3–4.6 and use layout(binding), which needs GLSL 420. Record those
// bindings, then rewrite the source so the 4.1 compiler can accept it.
// Shader storage blocks and compute shaders still need 4.3 and will not compile.
std::vector<UniformBlockBinding> collectUniformBlockBindings(const std::string& source) {
    std::vector<UniformBlockBinding> result;
#if defined(__APPLE__)
    const std::regex blockRe(R"(layout[ \t]*\(([^)]*)\)[ \t]*uniform[ \t]+([A-Za-z_][A-Za-z0-9_]*))");
    const std::regex bindRe(R"(binding[ \t]*=[ \t]*(\d+))");
    for (auto it = std::sregex_iterator(source.begin(), source.end(), blockRe);
         it != std::sregex_iterator(); ++it) {
        std::smatch bind;
        const std::string layout = (*it)[1].str();
        if (std::regex_search(layout, bind, bindRe)) {
            result.push_back({(*it)[2].str(), static_cast<GLuint>(std::stoi(bind[1].str()))});
        }
    }
#else
    (void)source;
#endif
    return result;
}

void adaptShaderForMac(std::string& source) {
#if defined(__APPLE__)
    source = std::regex_replace(
        source,
        std::regex(R"(#version[ \t]+\d+[ \t]*(core|compatibility)?)"),
        "#version 410 core");
    source = std::regex_replace(
        source,
        std::regex(R"(layout[ \t]*\([ \t]*early_fragment_tests[ \t]*\)[ \t]*in[ \t]*;)"),
        "");
    source = std::regex_replace(
        source,
        std::regex(R"(,?[ \t]*binding[ \t]*=[ \t]*\d+[ \t]*,?)"),
        "");
    source = std::regex_replace(source, std::regex(R"(layout[ \t]*\([ \t]*,)"), "layout(");
    source = std::regex_replace(source, std::regex(R"(,[ \t]*\))"), ")");
#else
    (void)source;
#endif
}

void applyUniformBlockBindings(GLuint program, const std::vector<UniformBlockBinding>& bindings) {
    for (const auto& item : bindings) {
        GLuint index = glGetUniformBlockIndex(program, item.name.c_str());
        if (index != GL_INVALID_INDEX) {
            glUniformBlockBinding(program, index, item.binding);
        }
    }
}
}

Shader::Shader(const char* vertexPath, const char* fragmentPath, const char* geometryPath,const char* tessControlPath,const char* tessEvalPath)
{
    this->requireMat = true;
    // 1. retrieve the vertex/fragment source code from filePath
    std::string vertexCode;
    std::string fragmentCode;
    std::string geometryCode;
    std::string tessControlCode;
    std::string tessEvalCode;
    std::ifstream vShaderFile;
    std::ifstream fShaderFile;
    std::ifstream gShaderFile;
    std::ifstream tessCShaderFile;
    std::ifstream tessEShaderFile;
    // ensure ifstream objects can throw exceptions:
    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    gShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    tessCShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    tessEShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try
    {
        // open files
        vShaderFile.open(vertexPath);
        fShaderFile.open(fragmentPath);
        std::stringstream vShaderStream, fShaderStream;
        // read file's buffer contents into streams
        vShaderStream << vShaderFile.rdbuf();
        fShaderStream << fShaderFile.rdbuf();
        // close file handlers
        vShaderFile.close();
        fShaderFile.close();
        // convert stream into string
        vertexCode = vShaderStream.str();
        fragmentCode = fShaderStream.str();
        // if geometry shader path is present, also load a geometry shader
        if (geometryPath != nullptr)
        {
            gShaderFile.open(geometryPath);
            std::stringstream gShaderStream;
            gShaderStream << gShaderFile.rdbuf();
            gShaderFile.close();
            geometryCode = gShaderStream.str();
        }
        if (tessControlPath != nullptr) {
            tessCShaderFile.open(tessControlPath);
            std::stringstream tcShaderStream;
            tcShaderStream << tessCShaderFile.rdbuf();
            tessCShaderFile.close();
            tessControlCode = tcShaderStream.str();
        }
        if (tessEvalPath != nullptr) {
            tessEShaderFile.open(tessEvalPath);
            std::stringstream teShaderStream;
            teShaderStream << tessEShaderFile.rdbuf();
            tessEShaderFile.close();
            tessEvalCode = teShaderStream.str();
        }
    }
    catch (std::ifstream::failure& e)
    {
        std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
    }
    std::vector<UniformBlockBinding> blockBindings = collectUniformBlockBindings(vertexCode);
    for (const std::string* stage : {&fragmentCode, &geometryCode, &tessControlCode, &tessEvalCode}) {
        auto extra = collectUniformBlockBindings(*stage);
        blockBindings.insert(blockBindings.end(), extra.begin(), extra.end());
    }
    adaptShaderForMac(vertexCode);
    adaptShaderForMac(fragmentCode);
    adaptShaderForMac(geometryCode);
    adaptShaderForMac(tessControlCode);
    adaptShaderForMac(tessEvalCode);
    const char* vShaderCode = vertexCode.c_str();
    const char* fShaderCode = fragmentCode.c_str();
    // 2. compile shaders
    unsigned int vertex, fragment;
    // vertex shader
    vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, NULL);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX",vertexPath);
    // fragment Shader
    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, NULL);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT",fragmentPath);
    // if geometry shader is given, compile geometry shader
    unsigned int geometry;
    if (geometryPath != nullptr)
    {
        const char* gShaderCode = geometryCode.c_str();
        geometry = glCreateShader(GL_GEOMETRY_SHADER);
        glShaderSource(geometry, 1, &gShaderCode, NULL);
        glCompileShader(geometry);
        checkCompileErrors(geometry, "GEOMETRY",geometryPath);
    }
    unsigned int tessControl, tessEval;
    if (tessControlPath != nullptr) {
        const char* tcShaderCode = tessControlCode.c_str();
        tessControl = glCreateShader(GL_TESS_CONTROL_SHADER);
        glShaderSource(tessControl, 1,&tcShaderCode, NULL);
        glCompileShader(tessControl);
        checkCompileErrors(tessControl, "TESS CONTROL",tessControlPath);
    }
    if (tessEvalPath != nullptr) {
        const char* teShaderCode = tessEvalCode.c_str();
        tessEval = glCreateShader(GL_TESS_EVALUATION_SHADER);
        glShaderSource(tessEval, 1,&teShaderCode, NULL);
        glCompileShader(tessEval);
        checkCompileErrors(tessEval, "TESS EVAL",tessEvalPath);
    }
    // shader Program
    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    if (geometryPath != nullptr)
        glAttachShader(ID, geometry);
    if (tessControlPath != nullptr)
        glAttachShader(ID, tessControl);
    if (tessEvalPath != nullptr)
        glAttachShader(ID, tessEval);
    glLinkProgram(ID);
    GLint linkStatus = GL_FALSE;
    glGetProgramiv(ID, GL_LINK_STATUS, &linkStatus);
    linked = linkStatus == GL_TRUE;
    if (linked) {
        applyUniformBlockBindings(ID, blockBindings);
    }
    checkCompileErrors(ID, "PROGRAM", vertexPath);
    // delete the shaders as they're linked into our program now and no longer necessery
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (geometryPath != nullptr)
        glDeleteShader(geometry);
    if (tessControlPath != nullptr)
        glDeleteShader(tessControl);
    if (tessEvalPath != nullptr)
        glDeleteShader(tessEval);
}

Shader::Shader(const char* computePath)
{
    requireMat = false;
    // 1. retrieve the vertex/fragment source code from filePath
    std::string computeCode;
    std::ifstream computeShaderFile;
    // ensure ifstream objects can throw exceptions:
    computeShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try
    {
        // open files
        computeShaderFile.open(computePath);
        std::stringstream computeShaderStream;
        // read file's buffer contents into streams
        computeShaderStream << computeShaderFile.rdbuf();
        // close file handlers
        computeShaderFile.close();
        // convert stream into string
        computeCode= computeShaderStream.str();
    }
    catch (std::ifstream::failure& e)
    {
        std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
    }
    adaptShaderForMac(computeCode);
    const char* computeShaderCode = computeCode.c_str();
    // 2. compile shaders
    unsigned int compute;
    // vertex shader
    compute = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(compute, 1, &computeShaderCode, NULL);
    glCompileShader(compute);
    checkCompileErrors(compute, "COMPUTE",computePath);
    // shader Program
    ID = glCreateProgram();
    glAttachShader(ID, compute);
    glLinkProgram(ID);
    GLint linkStatus = GL_FALSE;
    glGetProgramiv(ID, GL_LINK_STATUS, &linkStatus);
    linked = linkStatus == GL_TRUE;
    checkCompileErrors(ID, "PROGRAM",computePath);
    // delete the shaders as they're linked into our program now and no longer necessery
    glDeleteShader(compute);
}

// activate the shader
// ------------------------------------------------------------------------
void Shader::use()
{
    if (!linked) {
        return;
    }
    glUseProgram(ID);
}
// utility uniform functions
// ------------------------------------------------------------------------
void Shader::setBool(const std::string& name, bool value) const
{
    if (!linked) return;
    glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
}
// ------------------------------------------------------------------------
void Shader::setInt(const std::string& name, int value) const
{
    if (!linked) return;
    glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
}
void Shader::setUInt(const std::string& name, unsigned int value)const {
    if (!linked) return;
    glUniform1ui(glGetUniformLocation(ID, name.c_str()), value);
}
// ------------------------------------------------------------------------
void Shader::setFloat(const std::string& name, float value) const
{
    if (!linked) return;
    glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
}
// ------------------------------------------------------------------------
void Shader::setVec2(const std::string& name, const glm::vec2& value) const
{
    if (!linked) return;
    glUniform2fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}
void Shader::setVec2(const std::string& name, float x, float y) const
{
    if (!linked) return;
    glUniform2f(glGetUniformLocation(ID, name.c_str()), x, y);
}
// ------------------------------------------------------------------------
void Shader::setVec3(const std::string& name, const glm::vec3& value) const
{
    if (!linked) return;
    glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}
void Shader::setVec3(const std::string& name, float x, float y, float z) const
{
    if (!linked) return;
    glUniform3f(glGetUniformLocation(ID, name.c_str()), x, y, z);
}
// ------------------------------------------------------------------------
void Shader::setVec4(const std::string& name, const glm::vec4& value) const
{
    if (!linked) return;
    glUniform4fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}
void Shader::setVec4(const std::string& name, float x, float y, float z, float w)
{
    if (!linked) return;
    glUniform4f(glGetUniformLocation(ID, name.c_str()), x, y, z, w);
}
// ------------------------------------------------------------------------
void Shader::setMat2(const std::string& name, const glm::mat2& mat) const
{
    if (!linked) return;
    glUniformMatrix2fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}
// ------------------------------------------------------------------------
void Shader::setMat3(const std::string& name, const glm::mat3& mat) const
{
    if (!linked) return;
    glUniformMatrix3fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}
// ------------------------------------------------------------------------
void Shader::setMat4(const std::string& name, const glm::mat4& mat) const
{
    if (!linked) return;
    glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}

// utility function for checking shader compilation/linking errors.
// ------------------------------------------------------------------------
void Shader::checkCompileErrors(unsigned int shader, std::string type,const char* path)
{
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM")
    {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            std::cout << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
            std::cout << "Path: " << path << '\n';
        }
    }
    else
    {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success)
        {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            std::cout << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
			std::cout << "Path: " << path << '\n';
        }
    }
}

void Shader::setUniformBuffer(const std::string& name, int binding)const {
    if (!linked) return;
    glUniformBlockBinding(ID,
        glGetUniformBlockIndex(ID, name.c_str()),binding
    );
}
