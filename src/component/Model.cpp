#include "component/Model.h"
#include "component/Mesh_Filter.h"
#include "component/Model.h"
#include "renderer/Material.h"
#include "system/ResourceManager.h"
#include "utils/log.h"
#include <glad/glad.h>
#include <iostream>
#include <tinygltf/tiny_gltf.h>

std::shared_ptr<Mesh> AssimpLoader::combineMesh(const std::vector<std::shared_ptr<Mesh>> &meshes)
{
	// TODO:
	std::shared_ptr<Mesh> resultMesh = std::make_shared<Mesh>();
	int beginIndex = 0;
	for (auto &submesh : meshes)
	{
		auto &resultVertices = resultMesh->vertices;
		auto &resultIndices = resultMesh->indices;
		auto &subVertices = submesh->vertices;
		auto &subIndices = submesh->indices;
		resultVertices.insert(resultVertices.end(), subVertices.begin(), subVertices.end());
		// indices append
		for (auto &index : subIndices)
		{
			resultIndices.emplace_back(index + beginIndex);
		}
		beginIndex = resultVertices.size();
	}
	return resultMesh;
}

vector<shared_ptr<Mesh>> AssimpLoader::loadModel(const std::string &path, bool flipUV)
{
	std::vector<std::shared_ptr<Mesh>> meshes;
	Assimp::Importer importer;
	unsigned int pFlags = aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace;
	if (flipUV)
		pFlags |= aiProcess_FlipUVs;
	const aiScene *scene = importer.ReadFile(path, pFlags);
	if (!scene) //|| scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
	{
		LOG_ERROR("ERROR::ASSIMP:: Failed to load file: " << path);
		return {};
	}

	// process ASSIMP's root node recursively
	processNode(meshes, scene->mRootNode, scene);

	// return combineMesh(meshes);
	return meshes;
}

void AssimpLoader::processNode(std::vector<std::shared_ptr<Mesh>> &meshes, aiNode *node, const aiScene *scene)
{
	// process each mesh located at the current node
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		// the node object only contains indices to index the actual objects in the scene.
		// the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
		aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
		meshes.push_back(processMesh(mesh, scene));
	}
	// after we've processed all of the meshes (if any) we then recursively process each of the children nodes
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		processNode(meshes, node->mChildren[i], scene);
	}
}

std::shared_ptr<Mesh> AssimpLoader::processMesh(aiMesh *mesh, const aiScene *scene)
{
	// data to fill
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;
	// std::vector<Texture> textures;
	// std::shared_ptr<Material> material;

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
	aiMaterial *aimaterial = scene->mMaterials[mesh->mMaterialIndex];
	// we assume a convention for sampler names in the shaders. Each diffuse texture should be named
	// as 'texture_diffuseN' where N is a sequential number ranging from 1 to MAX_SAMPLER_NUMBER.
	// Same applies to other texture as the following list summarizes:
	// diffuse: texture_diffuseN
	// specular: texture_specularN
	// normal: texture_normalN

	// auto material = std::make_shared<Material>();
	// //// 1. diffuse maps
	// loadMaterialTextures(aimaterial, aiTextureType_DIFFUSE, "material.albedo",material);
	// //// 2. specular maps
	// loadMaterialTextures(aimaterial, aiTextureType_SPECULAR, "material.metallic",material);
	// //// 3. normal maps
	// loadMaterialTextures(aimaterial, aiTextureType_HEIGHT, "material.height",material);
	// //// 4. height maps
	// loadMaterialTextures(aimaterial, aiTextureType_AMBIENT, "",material); //TODO:

	// loadMaterialTextures(aimaterial, aiTextureType_AMBIENT_OCCLUSION, "material.ao", material);

	// loadMaterialTextures(aimaterial, aiTextureType_AMBIENT, "", material);
	//  return a mesh object created from the extracted mesh data
	return std::make_shared<Mesh>(vertices, indices);
}

// checks all material textures of a given type and loads the textures if they're not loaded yet.
// the required info is returned as a Texture struct.
void AssimpLoader::loadMaterialTextures(aiMaterial *mat, aiTextureType type, std::string texType, std::shared_ptr<Material> &material)
{
	for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
	{
		aiString str;
		mat->GetTexture(type, i, &str);
		std::string mat_path(str.C_Str());
		const std::shared_ptr<Texture> &tex = resourceManager->getResource(mat_path);

		material->textures.insert({texType, tex});
	}
}

const uint8_t *getAccessorDataAddress(const tinygltf::Model &model, const tinygltf::Accessor &accessor)
{
	auto &bufferView = model.bufferViews[accessor.bufferView];
	auto &buffer = model.buffers[bufferView.buffer];

	int accessor_offset = accessor.byteOffset;
	int bufferview_offset = bufferView.byteOffset;

	const uint8_t *start_addr = buffer.data.data() + bufferview_offset + accessor_offset;
	return start_addr;
}

std::shared_ptr<Mesh> buildMesh(const tinygltf::Model &model, unsigned int meshIndex)
{
	//   auto meshPrimitives =
	//   std::make_shared<std::vector<std::shared_ptr<Primitive>>>();
	const auto &primitives = model.meshes[meshIndex].primitives;

	// for (auto i = 0; i < primitives.size(); ++i)
	// {
	// build primitives
	// TODO: onlyh primitives 0 is parsed
	auto prim = primitives[0];

	vector<unsigned int> indices;
	// vertex
	// calculate vertex size;
	int size = model.accessors[prim.attributes.begin()->second].count;
	vector<Vertex> vertices(size);

	for (auto a : prim.attributes)
	{
		string name = a.first;
		int accessor_index = a.second;
		auto &accessor = model.accessors[accessor_index];
		float *address = (float *)getAccessorDataAddress(model, accessor);
		int cnt = accessor.count;

		if (name == "NORMAL")
		{

			for (int i = 0; i < cnt; i++)
			{
				vertices[i].Normal = {address[3 * i], address[3 * i + 1], address[3 * i + 2]};
			}
		}
		else if (name == "POSITION")
		{
			for (int i = 0; i < cnt; i++)
			{
				vertices[i].Position = {address[3 * i], address[3 * i + 1], address[3 * i + 2]};
			}
		}
		else if (name == "TANGENT")
		{

			for (int i = 0; i < cnt; i++)
			{
				vertices[i].Tangent = {address[3 * i], address[3 * i + 1], address[3 * i + 2]};
			}
		}
		else if (name.substr(0, 8) == "TEXCOORD")
		{

			// texcoord
			for (int i = 0; i < cnt; i++)
			{
				vertices[i].TexCoords = {address[2 * i], address[2 * i + 1]};
			}
		}
	}

	// indices
	auto indiceAccessor = model.accessors[prim.indices];
	int cnt = indiceAccessor.count; // number of elements
	auto index_start = getAccessorDataAddress(model, indiceAccessor);
	indices = vector<unsigned int>(index_start, index_start + cnt * sizeof(unsigned int)); // 初始化
	// }

	auto mesh = std::make_shared<Mesh>(vertices, indices);
	return mesh;
}

// gltf::mesh 对应 GameObject
// gltf::Primitvies 对应 Mesh
vector<shared_ptr<Mesh>> GLTFLoader::loadModel(const std::string &path, bool flipUV)
{
	tinygltf::TinyGLTF loader;
	string err;
	string warn;
	tinygltf::Model gltfModel;
	loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path);
	// 暂定只 build mesh 0
	buildMesh(gltfModel, 0);

	return vector<shared_ptr<Mesh>>();
}
