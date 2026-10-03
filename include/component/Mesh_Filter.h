#pragma once
#include "engine/LogicAsset.h"

#include "../component/Component.h"
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <vector>
#include "utils/header.h"

using std::shared_ptr;
using std::vector;

class Material;
class Texture;

const int MAX_BONE_INFLUENCE = 4;
// const double PI = 3.1415926;

enum class SHAPE { SPHERE, PLANE, POINT, CUBE };
struct Vertex {
    glm::vec3 Position{0};
    glm::vec3 Normal{0, 1, 0};
    glm::vec2 TexCoords{0};
    glm::vec3 Tangent{1, 0, 0};
    glm::vec3 Bitangent{0, 0, 1};

    // int m_BoneIDs[MAX_BONE_INFLUENCE];
    // float m_Weights[MAX_BONE_INFLUENCE];
};

typedef unsigned int GLuint;
class Mesh : public engine::LogicAsset {
  public:
    const std::vector<Vertex> &getVertices() const {
        checkLogicThread();
        return vertices;
    }
    const std::vector<unsigned> &getIndices() const {
        checkLogicThread();
        return indices;
    }
    std::shared_ptr<Material> getMaterial() const {
        checkLogicThread();
        return material;
    }
    const std::string &getName() const {
        checkLogicThread();
        return name;
    }
    void setName(std::string value) {
        checkLogicThread();
        name = std::move(value);
    }
    void setMaterial(std::shared_ptr<Material>);
    void setGeometry(std::vector<Vertex>, std::vector<unsigned>);
    GLuint getVAO() const {
        checkLogicThread();
        return VAO;
    }
    GLuint getEBO() const {
        checkLogicThread();
        return EBO;
    }

  private:
    friend class GameObject;
    std::vector<Vertex> vertices;
    std::vector<unsigned> indices;
    std::shared_ptr<Material> material;
    std::string name;
    GLuint VAO = 0, VBO = 0, EBO = 0;
    uint64_t uploadedRevision_ = 0;

  public:
    Mesh();
    Mesh(const Mesh &);
    Mesh(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices);
    static std::shared_ptr<Mesh> initSphere(int pointNum);
    static std::shared_ptr<Mesh> initPlane();
    static std::shared_ptr<Mesh> initPoint();
    static std::shared_ptr<Mesh> initCube();
    void genVAO();
};

class MeshFilter : public Component {
  public:
    MeshFilter();

    ~MeshFilter();

    void addShape(SHAPE type);
    // void loadMesh(std::string path);
    void addMesh(std::shared_ptr<Mesh> mesh_);

    void setMesh(const vector<shared_ptr<Mesh>> &meshes);

    virtual void loadFromJson(json &data);

  public:
    const auto &getMeshes() const {
        checkLogicThread();
        return meshes;
    }

  private:
    friend class GameObject;
    std::vector<std::shared_ptr<Mesh>> meshes;

  private:
    void addShape(std::string type);
};
