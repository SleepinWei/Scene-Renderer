#include "renderer/SceneImport.h"
#include "engine/AssetPath.h"
#include <assimp/ProgressHandler.hpp>
#include "component/Mesh_Filter.h"
#include "renderer/Material.h"
#include "renderer/Texture.h"
#include <glad/glad.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <stb/stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace {
std::shared_ptr<Texture> pixels(int width, int height) {
    auto result = std::make_shared<Texture>();
    result->setPixels(width,height,4,std::vector<unsigned char>(size_t(width)*height*4));
    return result;
}
std::shared_ptr<Texture> constant(glm::vec3 value) {
    auto result = pixels(2,2);
    std::vector<unsigned char> pixelData(16);
    for (int i=0; i<4; ++i) {
        for (int c=0; c<3; ++c) pixelData.data()[i*4+c]=static_cast<unsigned char>(std::clamp(value[c],0.f,1.f)*255);
        pixelData.data()[i*4+3]=255;
    }
    result->setPixels(2,2,4,std::move(pixelData));
    return result;
}
std::string texturePath(const aiMaterial* material, aiTextureType type, const std::filesystem::path& directory) {
    aiString name;
    if (material->GetTexture(type,0,&name)!=AI_SUCCESS) return {};
    std::string relative=name.C_Str(); std::replace(relative.begin(),relative.end(),'\\','/');
    return (directory/relative).lexically_normal().string();
}
struct TextureCache {
    std::unordered_map<std::string,std::shared_ptr<Texture>> loaded;
    std::shared_ptr<Texture> load(const std::string& path) {
        if (path.empty()) return nullptr;
        if (loaded.count(path)) return loaded.at(path);
        auto texture=Texture::loadFromFileAsync(path,4);
        if(!texture->pixels())throw std::runtime_error("Missing scene texture: "+path);
        loaded[path]=texture; return texture;
    }
    std::shared_ptr<Texture> albedo(const std::string& path,const std::string& mask) {
        auto color=load(path);
        if (mask.empty()) return color;
        const std::string key=path+"|mask="+mask;
        if (loaded.count(key)) return loaded.at(key);
        auto opacity=load(mask);
        bool alphaMask=false;
        for(size_t p=3;p<size_t(opacity->getWidth())*opacity->getHeight()*4;p+=4) if(opacity->pixels()[p]<254) {alphaMask=true;break;}
        auto result=pixels(color?color->getWidth():opacity->getWidth(),color?color->getHeight():opacity->getHeight());
        std::vector<unsigned char> pixelData(size_t(result->getWidth())*result->getHeight()*4);
        for (int y=0;y<result->getHeight();++y) for (int x=0;x<result->getWidth();++x) {
            auto dst=pixelData.data()+(y*result->getWidth()+x)*4;
            auto src=opacity->pixels()+((y*opacity->getHeight()/result->getHeight())*opacity->getWidth()+x*opacity->getWidth()/result->getWidth())*4;
            for (int c=0;c<3;++c) dst[c]=color?color->pixels()[(y*result->getWidth()+x)*4+c]:255;
            dst[3]=static_cast<unsigned char>((color?color->pixels()[(y*result->getWidth()+x)*4+3]:255)*int(src[alphaMask?3:0])/255);
        }
        result->setPixels(result->getWidth(),result->getHeight(),4,std::move(pixelData));
        loaded[key]=result; return result;
    }
    std::shared_ptr<Texture> bump(const std::string& path) {
        if (path.empty()) return nullptr;
        const std::string key=path+"|normal-from-height";
        if (loaded.count(key)) return loaded.at(key);
        auto height=load(path); auto result=pixels(height->getWidth(),height->getHeight());
        std::vector<unsigned char> pixelData(size_t(result->getWidth())*result->getHeight()*4);
        auto sample=[&](int x,int y) {x=(x+height->getWidth())%height->getWidth();y=(y+height->getHeight())%height->getHeight();return height->pixels()[(y*height->getWidth()+x)*4]/255.f;};
        for (int y=0;y<height->getHeight();++y) for (int x=0;x<height->getWidth();++x) {
            glm::vec3 n=glm::normalize(glm::vec3(-2*(sample(x+1,y)-sample(x-1,y)),-2*(sample(x,y+1)-sample(x,y-1)),1));
            auto dst=pixelData.data()+(y*height->getWidth()+x)*4;
            for (int c=0;c<3;++c) dst[c]=static_cast<unsigned char>((n[c]*.5f+.5f)*255);
            dst[3]=255;
        }
        result->setPixels(result->getWidth(),result->getHeight(),4,std::move(pixelData));
        loaded[key]=result; return result;
    }
};
}
ImportedScene importOBJScene(const std::string& input,float height) {
    engine::CancellationScope::check();const auto path=engine::AssetPath::resolve(input);
    if (!std::filesystem::exists(path)) throw std::runtime_error("Missing GI scene: "+path+"; run python3 tools/fetch_gi_assets.py");
    std::cout<<"Importing "<<path<<std::endl;
    class Progress:public Assimp::ProgressHandler {bool Update(float)override{return !engine::CancellationScope::cancelled();}};
    Assimp::Importer importer;importer.SetProgressHandler(new Progress());
    auto source=importer.ReadFile(path,aiProcess_Triangulate|aiProcess_JoinIdenticalVertices|aiProcess_GenSmoothNormals|aiProcess_PreTransformVertices|aiProcess_OptimizeMeshes|aiProcess_SortByPType);
    if (!source) {engine::CancellationScope::check();throw engine::AssetLoadError("import",path,importer.GetErrorString());}
    const auto directory=std::filesystem::path(path).parent_path();
    TextureCache cache;
    auto flat=constant({.5f,.5f,1}),one=constant(glm::vec3(1)),zero=constant(glm::vec3(0));
    std::vector<std::shared_ptr<Material>> materials;
    for (unsigned i=0;i<source->mNumMaterials;++i) {
        auto src=source->mMaterials[i]; auto material=std::make_shared<Material>();
        aiColor3D diffuse(1,1,1);src->Get(AI_MATKEY_COLOR_DIFFUSE,diffuse);
        material->setAlbedoFactor({diffuse.r,diffuse.g,diffuse.b});
        float shininess=16;src->Get(AI_MATKEY_SHININESS,shininess);
        const float roughness=std::clamp(std::sqrt(2.f/(shininess+2.f)),.35f,.95f);
        auto albedo=cache.albedo(texturePath(src,aiTextureType_DIFFUSE,directory),texturePath(src,aiTextureType_OPACITY,directory));
        if (albedo) for (size_t p=3;p<size_t(albedo->getWidth())*albedo->getHeight()*4;p+=4) {
            if (albedo->pixels()[p]<250) { material->setAlphaCutoff(.45f);material->setTwoSided(true);break; }
        }
        auto normal=cache.load(texturePath(src,aiTextureType_NORMALS,directory));
        if (!normal) normal=cache.bump(texturePath(src,aiTextureType_HEIGHT,directory));
        material->addTexture(albedo?albedo:one,"material.albedo");
        material->addTexture(normal?normal:flat,"material.normal");
        material->addTexture(constant(glm::vec3(roughness)),"material.roughness");
        material->addTexture(zero,"material.metallic");material->addTexture(one,"material.ao");material->addTexture(zero,"material.height");
        materials.push_back(material);
    }
    ImportedScene result;
    result.low=glm::vec3(std::numeric_limits<float>::max());result.high=-result.low;
    for (unsigned m=0;m<source->mNumMeshes;++m) {
        auto src=source->mMeshes[m];
        if (!(src->mPrimitiveTypes&aiPrimitiveType_TRIANGLE)) continue;
        auto mesh=std::make_shared<Mesh>();mesh->setName(src->mName.C_Str());mesh->setMaterial(materials.at(src->mMaterialIndex));
        std::vector<Vertex> vertices(src->mNumVertices);std::vector<unsigned> indices;indices.reserve(size_t(src->mNumFaces)*3);
        for (unsigned i=0;i<src->mNumVertices;++i) {
            auto& v=vertices[i];v=Vertex{};
            v.Position={src->mVertices[i].x,src->mVertices[i].y,src->mVertices[i].z};
            v.Normal=src->HasNormals()?glm::normalize(glm::vec3(src->mNormals[i].x,src->mNormals[i].y,src->mNormals[i].z)):glm::vec3(0,1,0);
            v.TexCoords=src->HasTextureCoords(0)?glm::vec2(src->mTextureCoords[0][i].x,src->mTextureCoords[0][i].y):glm::vec2(v.Position.x,v.Position.z);
            auto axis=std::abs(v.Normal.y)<.95f?glm::vec3(0,1,0):glm::vec3(1,0,0);
            v.Tangent=glm::normalize(glm::cross(axis,v.Normal));v.Bitangent=glm::cross(v.Normal,v.Tangent);
            result.low=glm::min(result.low,v.Position);result.high=glm::max(result.high,v.Position);
        }
        for (unsigned i=0;i<src->mNumFaces;++i) {
            auto& face=src->mFaces[i];if(face.mNumIndices!=3)continue;
            indices.insert(indices.end(),face.mIndices,face.mIndices+3);
        }
        mesh->setGeometry(std::move(vertices),std::move(indices));
        result.triangles+=mesh->getIndices().size()/3; result.meshes.push_back(mesh);
    }
    const float scale=height/(result.high.y-result.low.y);
    const auto origin=glm::vec3((result.low.x+result.high.x)/2,result.low.y,(result.low.z+result.high.z)/2);
    std::cout<<"Source bounds: "<<result.low.x<<","<<result.low.y<<","<<result.low.z<<" to "<<result.high.x<<","<<result.high.y<<","<<result.high.z<<std::endl;
    for (const auto& mesh:result.meshes) {auto vertices=mesh->getVertices();for(auto& v:vertices)v.Position=(v.Position-origin)*scale;mesh->setGeometry(std::move(vertices),mesh->getIndices());}
    result.low=(result.low-origin)*scale; result.high=(result.high-origin)*scale;
    std::cout<<"Imported "<<result.meshes.size()<<" meshes, "<<result.triangles<<" triangles; height normalized to "<<height<<std::endl;
    return result;
}
