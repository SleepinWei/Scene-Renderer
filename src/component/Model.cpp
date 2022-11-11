#include<iostream>
#include"Model.h"
#include"../renderer/ResourceManager.h"
#include"../renderer/Material.h"
#include"../renderer/Mesh_Filter.h"

std::shared_ptr<Mesh> Model::combineMesh(const std::vector<std::shared_ptr<Mesh>>& meshes) {
	// TODO:
	std::shared_ptr<Mesh> resultMesh = std::make_shared<Mesh>();
	int beginIndex = 0;
	for (auto& submesh : meshes) {
		auto& resultVertices = resultMesh->vertices;
		auto& resultIndices = resultMesh->indices;
		auto& subVertices = submesh->vertices;
		auto& subIndices = submesh->indices;
		resultVertices.insert(resultVertices.end(), subVertices.begin(), subVertices.end());
		// indices append
		for (auto& index: subIndices) {
			resultIndices.emplace_back(index + beginIndex);
		}
		beginIndex = resultVertices.size();
	}
	return resultMesh;
}

std::shared_ptr<Mesh> Model::loadModel(const std:: string& path,bool flipUV) {
	std::vector<std::shared_ptr<Mesh>> meshes; 
	Assimp::Importer importer; 
	unsigned int pFlags = aiProcess_JoinIdenticalVertices|aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace;
	if (flipUV)
		pFlags |= aiProcess_FlipUVs;
	const aiScene* scene = importer.ReadFile(path,pFlags);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
	{
		std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return nullptr;
	}

	// process ASSIMP's root node recursively
	processNode(meshes,scene->mRootNode, scene);

	return combineMesh(meshes);
}

void Model::processNode(std::vector<std::shared_ptr<Mesh>>& meshes,aiNode* node, const aiScene* scene)
{
	// process each mesh located at the current node
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		// the node object only contains indices to index the actual objects in the scene. 
		// the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		meshes.push_back(processMesh(mesh, scene));
	}
	// after we've processed all of the meshes (if any) we then recursively process each of the children nodes
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		processNode(meshes,node->mChildren[i], scene);
	}
}

std::shared_ptr<Mesh> Model::processMesh(aiMesh* mesh, const aiScene* scene)
{
	// data to fill
	std::vector <Vertex> vertices;
	std::vector<unsigned int> indices;
	//std::vector<Texture> textures;
	std::shared_ptr<Material> material; 

	// walk through each of the mesh's vertices
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;
		glm::vec3 vector; // we declare a placeholder vector since assimp uses its own vector class that doesn't directly convert to glm's vec3 class so we transfer the data to this placeholder glm::vec3 first.
		// positions
		vector.x = mesh->mVertices[i].x;
		vector.y = mesh->mVertices[i].y;
		vector.z = mesh->mVertices[i].z;
		vertex.Position = vector;
		// normals
		if (mesh->HasNormals())
		{
			vector.x = mesh->mNormals[i].x;
			vector.y = mesh->mNormals[i].y;
			vector.z = mesh->mNormals[i].z;
			vertex.Normal = vector;
		}
		// texture coordinates
		if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
		{
			glm::vec2 vec;
			// a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
			// use models where a vertex can have multiple texture coordinates so we always take the first set (0).
			vec.x = mesh->mTextureCoords[0][i].x;
			vec.y = mesh->mTextureCoords[0][i].y;
			vertex.TexCoords = vec;
			// tangent
			vector.x = mesh->mTangents[i].x;
			vector.y = mesh->mTangents[i].y;
			vector.z = mesh->mTangents[i].z;
			vertex.Tangent = vector;
			// bitangent
			vector.x = mesh->mBitangents[i].x;
			vector.y = mesh->mBitangents[i].y;
			vector.z = mesh->mBitangents[i].z;
			vertex.Bitangent = vector;
		}
		else
			vertex.TexCoords = glm::vec2(0.0f, 0.0f);

		vertices.push_back(vertex);
	}
	// now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		// retrieve all indices of the face and store them in the indices vector
		for (unsigned int j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}
	// process materials
	//aiMaterial* aimaterial = scene->mMaterials[mesh->mMaterialIndex];
	// we assume a convention for sampler names in the shaders. Each diffuse texture should be named
	// as 'texture_diffuseN' where N is a sequential number ranging from 1 to MAX_SAMPLER_NUMBER. 
	// Same applies to other texture as the following list summarizes:
	// diffuse: texture_diffuseN
	// specular: texture_specularN
	// normal: texture_normalN

	//material = std::make_shared<Material>();
	//// 1. diffuse maps
	//loadMaterialTextures(aimaterial, aiTextureType_DIFFUSE, "material.albedo",material);
	//// 2. specular maps
	//loadMaterialTextures(aimaterial, aiTextureType_SPECULAR, "material.metallic",material);
	//// 3. normal maps
	//loadMaterialTextures(aimaterial, aiTextureType_HEIGHT, "material.height",material);
	//// 4. height maps
	//loadMaterialTextures(aimaterial, aiTextureType_AMBIENT, "",material); //TODO:

	//loadMaterialTextures(aimaterial, aiTextureType_AMBIENT_OCCLUSION, "material.ao", material);

	//loadMaterialTextures(aimaterial, aiTextureType_AMBIENT, "", material);
	// return a mesh object created from the extracted mesh data
	return std::make_shared<Mesh>(vertices, indices);
}

// checks all material textures of a given type and loads the textures if they're not loaded yet.
// the required info is returned as a Texture struct.
void Model::loadMaterialTextures(aiMaterial* mat, aiTextureType type,std::string texType, std::shared_ptr<Material>& material)
{
	for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
	{
		aiString str;
		mat->GetTexture(type, i, &str);
		std::string mat_path(str.C_Str());
		const std::shared_ptr<Texture>& tex = resourceManager->getResource(mat_path);

		material->textures.insert({ texType,tex });
	}
}


