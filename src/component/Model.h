#pragma once
#include<vector>
#include<glm/glm.hpp>
#include<memory>
#include<assimp/Importer.hpp>
#include<assimp/scene.h>
#include<assimp/postprocess.h>
#include<map>
#include<optional>

class ResourceManager;
class MeshFilter;
class Material;
extern std::unique_ptr<ResourceManager> resourceManager; 
//unsigned int TextureFromFile(const char* path, const std::string& directory, bool gamma = false);

class Model {
public:
    static std::optional<std::vector<std::shared_ptr<MeshFilter>>> loadModel(const std::string& path);
		
    static void processNode(std::vector<std::shared_ptr<MeshFilter>>& meshes,aiNode* node, const aiScene* scene);

    static std::shared_ptr<MeshFilter> processMesh(aiMesh* mesh, const aiScene* scene);

    // checks all material textures of a given type and loads the textures if they're not loaded yet.
    // the required info is returned as a Texture struct.
    static void loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::shared_ptr<Material>& material);
};

