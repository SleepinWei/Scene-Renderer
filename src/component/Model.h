#pragma once
#include<vector>
#include<glm/glm.hpp>
#include<memory>
#include<assimp/Importer.hpp>
#include<assimp/scene.h>
#include<assimp/postprocess.h>
#include<map>

class ResourceManager;
class MeshFilter;
class Material;
class Mesh;
extern std::unique_ptr<ResourceManager> resourceManager; 
//unsigned int TextureFromFile(const char* path, const std::string& directory, bool gamma = false);

class Model {
public:
    static std::shared_ptr<Mesh> combineMesh(const std::vector<std::shared_ptr<Mesh>>& meshes);

    static std::shared_ptr<MeshFilter> loadModel(const std::string& path, bool flipUV = true);
		
    static void processNode(std::vector<std::shared_ptr<Mesh>>& meshes,aiNode* node, const aiScene* scene);

    static std::shared_ptr<Mesh> processMesh(aiMesh* mesh, const aiScene* scene);

    // checks all material textures of a given type and loads the textures if they're not loaded yet.
    // the required info is returned as a Texture struct.
    static void loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string texType, std::shared_ptr<Material>& material);
};

