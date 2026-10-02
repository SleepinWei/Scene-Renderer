#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
class Mesh;
struct MetalImportedScene {
    std::vector<std::shared_ptr<Mesh>> meshes;
    glm::vec3 low, high;
    size_t triangles = 0;
};
MetalImportedScene importMetalOBJScene(const std::string& path, float height);
