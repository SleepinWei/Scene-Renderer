#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
class Mesh;
struct ImportedScene {
    std::vector<std::shared_ptr<Mesh>> meshes;
    glm::vec3 low, high;
    glm::vec3 sourceOrigin = glm::vec3(0);
    float sourceScale = 1;
    size_t triangles = 0;
};
struct SceneImportOptions {
    std::string convertedTextureDirectory;
    bool packedOrmSpecular = false;
    bool packedOcclusion = true;
    bool uvTangents = false;
};
ImportedScene importOBJScene(const std::string& path, float height, const SceneImportOptions& options = {});
