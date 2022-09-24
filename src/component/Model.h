#pragma once
#include<vector>
#include<glm/glm.hpp>
#include"../renderer/Mesh_Filter.h"
#include<memory>
#include<assimp/Importer.hpp>
#include<assimp/scene.h>
#include<assimp/postprocess.h>
#include<map>
#include"../renderer/ResourceManager.h"
#include<optional>

extern class ResourceManager resourceManager; 
//unsigned int TextureFromFile(const char* path, const std::string& directory, bool gamma = false);

namespace Model {
    std::optional<std::vector<std::shared_ptr<MeshFilter>>> loadModel(const std::string& path);
		
    void processNode(std::vector<std::shared_ptr<MeshFilter>> meshes,aiNode* node, const aiScene* scene);

    std::shared_ptr<MeshFilter> processMesh(aiMesh* mesh, const aiScene* scene);

    // checks all material textures of a given type and loads the textures if they're not loaded yet.
    // the required info is returned as a Texture struct.
    void loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::shared_ptr<Material> material);
};

