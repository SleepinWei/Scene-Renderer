#include "component/Model.h"
#include "component/Mesh_Filter.h"
#include "component/Model.h"
#include "renderer/Material.h"
#include "system/ResourceManager.h"
#include "utils/log.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>
#include <tinygltf/tiny_gltf.h>

using std::make_shared;
using std::shared_ptr;

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
		const std::shared_ptr<Texture> &tex = ResourceManager::GetInstance()->getResource(mat_path);

		material->textures.insert({texType, tex});
	}
}

const unsigned char *GLTFLoader::getAccessorDataAddress(const tinygltf::Model &model, const tinygltf::Accessor &accessor)
{
	const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
	const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

	int accessor_offset = accessor.byteOffset;
	int bufferview_offset = bufferView.byteOffset;

	const unsigned char *start_addr = (unsigned char *)buffer.data.data() + bufferview_offset + accessor_offset;
	return start_addr;
}

std::shared_ptr<Mesh> GLTFLoader::buildMesh(const tinygltf::Model &model, unsigned int meshIndex, glm::mat4 matrix, bool flipUV)
{
	const auto &primitives = model.meshes[meshIndex].primitives;

	// for (auto i = 0; i < primitives.size(); ++i)
	// {
	// build primitives
	// TODO: onlyh primitives 0 is parsed
	auto prim = primitives[0];

	// vertex
	// calculate vertex size;
	int size = model.accessors[prim.attributes.begin()->second].count;
	vector<Vertex> vertices(size);

	glm::mat3 normal_mat(glm::inverseTranspose(matrix));

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
				glm::vec3 normal = {address[3 * i], address[3 * i + 1], address[3 * i + 2]};
				glm::vec3 transformed_normal = normal_mat * normal;
				vertices[i].Normal = glm::normalize(transformed_normal);
			}
		}
		else if (name == "POSITION")
		{
			for (int i = 0; i < cnt; i++)
			{
				glm::vec4 pos = {address[3 * i], address[3 * i + 1], address[3 * i + 2], 1.0f};
				glm::vec4 result = matrix * pos;
				vertices[i].Position = {result.x / result.w, result.y / result.w, result.z / result.w};
			}
		}
		else if (name == "TANGENT")
		{

			for (int i = 0; i < cnt; i++)
			{
				glm::vec3 tangent = {address[3 * i], address[3 * i + 1], address[3 * i + 2]};
				glm::vec3 transformed_tangent = normal_mat * tangent;
				vertices[i].Tangent = glm::normalize(transformed_tangent);
			}
		}
		else if (name.substr(0, 8) == "TEXCOORD")
		{
			// texcoord
			for (int i = 0; i < cnt; i++)
			{
				if (flipUV)
					vertices[i].TexCoords = {address[2 * i], 1.0f - address[2 * i + 1]};
				else
					vertices[i].TexCoords = {address[2 * i], address[2 * i + 1]};
			}
		}
	}

	// indices
	auto &indiceAccessor = model.accessors[prim.indices];
	int indice_count = indiceAccessor.count; // number of elements
	auto index_start = (unsigned int *)getAccessorDataAddress(model, indiceAccessor);

	vector<unsigned int> indices(indice_count);
	for (int i = 0; i < indice_count; i++)
	{
		indices[i] = index_start[i];
	}
	// }

	// materials
	shared_ptr<Material> mesh_mat = make_shared<Material>();

	int material_index = prim.material;
	const string directory = path.substr(0, path.find_last_of("/") + 1);
	auto mat = model.materials[material_index];
	{
		if (mat.normalTexture.index >= 0)
		{
			auto normal = model.images[model.textures[mat.normalTexture.index].source];
			mesh_mat->addTextureAsync(directory + normal.uri, "material.normal");
		}
		if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0)
		{
			auto baseColor = model.images[model.textures[mat.pbrMetallicRoughness.baseColorTexture.index].source];
			mesh_mat->addTextureAsync(directory + baseColor.uri, "material.albedo");
		}
		if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
		{
			auto metallicRoughness = model.images[model.textures[mat.pbrMetallicRoughness.metallicRoughnessTexture.index].source];
			mesh_mat->addTextureAsync(directory + metallicRoughness.uri, "material.roughness");
			mesh_mat->addTextureAsync(directory + metallicRoughness.uri, "material.metallic");
		}
	}

	auto mesh = std::make_shared<Mesh>(vertices, indices);
	mesh->material = mesh_mat;
	return mesh;
}

void GLTFLoader::buildNode(const tinygltf::Model &model, vector<shared_ptr<Mesh>> &meshes, unsigned int node_index, glm::mat4 parent_matrix, bool flipUV)
{
	const std::vector<double> &nodeMatrix = model.nodes[node_index].matrix;
	glm::mat4 matrix(1.0f);
	if (nodeMatrix.size() == 16)
	{
		matrix[0].x = nodeMatrix[0], matrix[0].y = nodeMatrix[1],
		matrix[0].z = nodeMatrix[2], matrix[0].w = nodeMatrix[3];
		matrix[1].x = nodeMatrix[4], matrix[1].y = nodeMatrix[5],
		matrix[1].z = nodeMatrix[6], matrix[1].w = nodeMatrix[7];
		matrix[2].x = nodeMatrix[8], matrix[2].y = nodeMatrix[9],
		matrix[2].z = nodeMatrix[10], matrix[2].w = nodeMatrix[11];
		matrix[3].x = nodeMatrix[12], matrix[3].y = nodeMatrix[13],
		matrix[3].z = nodeMatrix[14], matrix[3].w = nodeMatrix[15];
	}
	else
	{
		if (model.nodes[node_index].translation.size() == 3)
		{
			glm::translate(matrix, glm::vec3(model.nodes[node_index].translation[0],
											 model.nodes[node_index].translation[1],
											 model.nodes[node_index].translation[2]));
		}
		if (model.nodes[node_index].rotation.size() == 4)
		{
			matrix *= glm::mat4_cast(glm::quat(model.nodes[node_index].rotation[3],
											   model.nodes[node_index].rotation[0],
											   model.nodes[node_index].rotation[1],
											   model.nodes[node_index].rotation[2]));
		}
		if (model.nodes[node_index].scale.size() == 3)
		{
			glm::scale(matrix, glm::vec3(model.nodes[node_index].scale[0],
										 model.nodes[node_index].scale[1],
										 model.nodes[node_index].scale[2]));
		}
	}

	glm::mat4 worldCoordMat = parent_matrix * matrix;
	// glm::mat4 worldCoordMat = matrix;
	if (model.nodes[node_index].mesh >= 0)
	{
		auto mesh = buildMesh(model, model.nodes[node_index].mesh, worldCoordMat, flipUV);

		meshes.push_back(mesh);
	}
	else
	{
		for (auto &childNodeIndex : model.nodes[node_index].children)
		{
			buildNode(model, meshes, childNodeIndex, worldCoordMat, flipUV);
		}
	}
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

	this->path = path;

	vector<shared_ptr<Mesh>> meshes;
	buildNode(gltfModel, meshes, 0, glm::mat4(), flipUV);
	return meshes;
}
