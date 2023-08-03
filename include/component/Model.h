#pragma once
#include<vector>
#include<glm/glm.hpp>
#include<memory>
#include<assimp/Importer.hpp>
#include<assimp/scene.h>
#include<assimp/postprocess.h>
#include<tinygltf/tiny_gltf.h>
#include<map>
#include<string>

class ResourceManager;
class MeshFilter;
class Material;
class Mesh;
extern std::unique_ptr<ResourceManager> resourceManager; 

using std::shared_ptr; 
using std::string; 
using std::vector; 
//unsigned int TextureFromFile(const char* path, const std::string& directory, bool gamma = false);

class ModelLoaderBase{
public:
    virtual vector<shared_ptr<Mesh>> loadModel(const string& path,bool flipUV = true) = 0;
};

class AssimpLoader:public ModelLoaderBase{
public:
    vector<shared_ptr<Mesh>> loadModel(const std::string& path,bool flipUV = true);
private:
    std::shared_ptr<Mesh> combineMesh(const std::vector<std::shared_ptr<Mesh>>& meshes);
		
    void processNode(std::vector<std::shared_ptr<Mesh>>& meshes,aiNode* node, const aiScene* scene);

    std::shared_ptr<Mesh> processMesh(aiMesh* mesh, const aiScene* scene);

    // checks all material textures of a given type and loads the textures if they're not loaded yet.
    // the required info is returned as a Texture struct.
    void loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string texType, std::shared_ptr<Material>& material);
};

class GLTFLoader: public ModelLoaderBase{
public:
    vector<shared_ptr<Mesh>> loadModel(const std::string& path,bool flipUV = true);

private:
    string path;

    const unsigned char*getAccessorDataAddress(const tinygltf::Model &model, const tinygltf::Accessor &accessor);
    shared_ptr<Mesh> buildMesh(const tinygltf::Model &model, unsigned int meshIndex, glm::mat4 matrix, bool flipUV);
    void buildNode(const tinygltf::Model &model,vector<shared_ptr<Mesh>>& meshes, unsigned int node_index, glm::mat4 parent_matrix, bool flipUV);
};
