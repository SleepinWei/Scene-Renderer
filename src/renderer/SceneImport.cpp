#include "renderer/SceneImport.h"
#include "engine/AssetPath.h"
#include "rhi/MemorySafety.h"
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
std::string texturePath(const aiMaterial* material, aiTextureType type, const std::filesystem::path& directory,
                        const SceneImportOptions& options) {
    aiString name;
    if (material->GetTexture(type,0,&name)!=AI_SUCCESS) return {};
    std::string relative=name.C_Str(); std::replace(relative.begin(),relative.end(),'\\','/');
    if (!options.convertedTextureDirectory.empty()) {
        auto converted = std::filesystem::path(relative).filename();
        converted += ".png"; // Retain DDS/TGA suffixes to avoid basename collisions.
        return (std::filesystem::path(engine::AssetPath::resolve(options.convertedTextureDirectory))/converted).string();
    }
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
    std::shared_ptr<Texture> channel(const std::string& path, unsigned channel) {
        if (path.empty()) return nullptr;
        const auto key=path+"|channel="+std::to_string(channel);
        if (loaded.count(key)) return loaded.at(key);
        auto packed=load(path);auto result=pixels(packed->getWidth(),packed->getHeight());
        std::vector<unsigned char> data(size_t(packed->getWidth())*packed->getHeight()*4);
        for(size_t i=0;i<data.size();i+=4){for(unsigned c=0;c<3;++c)data[i+c]=packed->pixels()[i+channel];data[i+3]=255;}
        result->setPixels(packed->getWidth(),packed->getHeight(),4,std::move(data));
        loaded[key]=result;return result;
    }
};
}
ImportedScene importOBJScene(const std::string& input,float height,const SceneImportOptions& options) {
    engine::CancellationScope::check();const auto path=engine::AssetPath::resolve(input);
    if (!std::filesystem::exists(path)) throw std::runtime_error("Missing GI scene: "+path+"; run python3 tools/fetch_gi_assets.py");
    auto importScratch=rhi::memorySafety().reserve(rhi::MemorySafety::bytes(size_t(std::filesystem::file_size(path)),8),"scene import working set");
    std::cout<<"Importing "<<path<<std::endl;
    class Progress:public Assimp::ProgressHandler {bool Update(float)override{
        if(engine::CancellationScope::cancelled())return false;
        try{rhi::memorySafety().check(0,"scene import continuation");return true;}catch(const rhi::ResourceBudgetExceeded&){return false;}
    }};
    Assimp::Importer importer;importer.SetProgressHandler(new Progress());
    unsigned flags=aiProcess_Triangulate|aiProcess_JoinIdenticalVertices|aiProcess_GenSmoothNormals|aiProcess_PreTransformVertices|aiProcess_OptimizeMeshes|aiProcess_SortByPType;
    auto source=importer.ReadFile(path,flags);
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
        auto albedo=cache.albedo(texturePath(src,aiTextureType_DIFFUSE,directory,options),texturePath(src,aiTextureType_OPACITY,directory,options));
        if (albedo) for (size_t p=3;p<size_t(albedo->getWidth())*albedo->getHeight()*4;p+=4) {
            if (albedo->pixels()[p]<250) { material->setAlphaCutoff(.45f);material->setTwoSided(true);break; }
        }
        auto normal=cache.load(texturePath(src,aiTextureType_NORMALS,directory,options));
        if (!normal) normal=cache.bump(texturePath(src,aiTextureType_HEIGHT,directory,options));
        const auto orm=options.packedOrmSpecular?texturePath(src,aiTextureType_SPECULAR,directory,options):std::string();
        material->addTexture(albedo?albedo:one,"material.albedo");
        material->addTexture(normal?normal:flat,"material.normal");
        material->addTexture(orm.empty()?constant(glm::vec3(roughness)):cache.channel(orm,1),"material.roughness");
        material->addTexture(orm.empty()?zero:cache.channel(orm,2),"material.metallic");
        material->addTexture(orm.empty()||!options.packedOcclusion?one:cache.channel(orm,0),"material.ao");material->addTexture(zero,"material.height");
        materials.push_back(material);
    }
    ImportedScene result;
    result.low=glm::vec3(std::numeric_limits<float>::max());result.high=-result.low;
    for (unsigned m=0;m<source->mNumMeshes;++m) {
        auto src=source->mMeshes[m];
        if (!(src->mPrimitiveTypes&aiPrimitiveType_TRIANGLE)) continue;
        auto mesh=std::make_shared<Mesh>();mesh->setName(src->mName.C_Str());mesh->setMaterial(materials.at(src->mMaterialIndex));
        std::vector<Vertex> vertices(src->mNumVertices);std::vector<unsigned> indices;indices.reserve(size_t(src->mNumFaces)*3);
        bool needsNormals=false;
        for (unsigned i=0;i<src->mNumVertices;++i) {
            auto& v=vertices[i];v=Vertex{};
            v.Position={src->mVertices[i].x,src->mVertices[i].y,src->mVertices[i].z};
            v.Normal=src->HasNormals()?glm::vec3(src->mNormals[i].x,src->mNormals[i].y,src->mNormals[i].z):glm::vec3(0);
            // Degenerate OBJ faces can yield zero/NaN generated normals.
            const float normalLength=glm::dot(v.Normal,v.Normal);
            if(std::isfinite(normalLength) && normalLength>1e-12f)v.Normal/=std::sqrt(normalLength);
            else {v.Normal=glm::vec3(0);needsNormals=true;}
            v.TexCoords=src->HasTextureCoords(0)?glm::vec2(src->mTextureCoords[0][i].x,src->mTextureCoords[0][i].y):glm::vec2(v.Position.x,v.Position.z);
            result.low=glm::min(result.low,v.Position);result.high=glm::max(result.high,v.Position);
        }
        for (unsigned i=0;i<src->mNumFaces;++i) {
            auto& face=src->mFaces[i];if(face.mNumIndices!=3)continue;
            indices.insert(indices.end(),face.mIndices,face.mIndices+3);
        }
        std::vector<glm::vec3> reconstructed(needsNormals?vertices.size():0,glm::vec3(0));
        if(needsNormals)for(size_t i=0;i<indices.size();i+=3){
            const auto a=indices[i],b=indices[i+1],c=indices[i+2];
            const auto area=glm::cross(vertices[b].Position-vertices[a].Position,vertices[c].Position-vertices[a].Position);
            if(std::isfinite(glm::dot(area,area))){reconstructed[a]+=area;reconstructed[b]+=area;reconstructed[c]+=area;}
        }
        // Derive both axes from UV derivatives after node transforms. In
        // particular, retain the sign on mirrored UV charts instead of forcing
        // every bitangent to cross(N,T), which reverses normal-map relief.
        std::vector<glm::vec3> tangents(options.uvTangents?vertices.size():0,glm::vec3(0));
        auto bitangents=tangents;
        if(options.uvTangents)for(size_t i=0;i<indices.size();i+=3){
            const auto a=indices[i],b=indices[i+1],c=indices[i+2];
            const auto e1=vertices[b].Position-vertices[a].Position,e2=vertices[c].Position-vertices[a].Position;
            const auto uv1=vertices[b].TexCoords-vertices[a].TexCoords,uv2=vertices[c].TexCoords-vertices[a].TexCoords;
            const float determinant=uv1.x*uv2.y-uv1.y*uv2.x;
            const float area=glm::length(glm::cross(e1,e2));
            if(!std::isfinite(determinant)||std::abs(determinant)<1e-12f||!std::isfinite(area)||area<1e-12f)continue;
            const auto t=(e1*uv2.y-e2*uv1.y)*(area/determinant);
            const auto bt=(e2*uv1.x-e1*uv2.x)*(area/determinant);
            if(!std::isfinite(glm::dot(t,t))||!std::isfinite(glm::dot(bt,bt)))continue;
            for(auto index:{a,b,c}){tangents[index]+=t;bitangents[index]+=bt;}
        }
        for(size_t i=0;i<vertices.size();++i){
            auto& v=vertices[i];
            if(glm::dot(v.Normal,v.Normal)==0){
                const float length=glm::dot(reconstructed[i],reconstructed[i]);
                v.Normal=std::isfinite(length)&&length>1e-12f?reconstructed[i]/std::sqrt(length):glm::vec3(0,1,0);
            }
            const auto sourceTangent=options.uvTangents?tangents[i]:glm::vec3(0);
            const auto tangent=sourceTangent-v.Normal*glm::dot(v.Normal,sourceTangent);
            const float length=glm::dot(tangent,tangent);
            if(options.uvTangents&&std::isfinite(length)&&length>1e-12f){
                const float handedness=glm::dot(glm::cross(v.Normal,tangent),bitangents[i])<0?-1.f:1.f;
                v.Tangent=tangent/std::sqrt(length);v.Bitangent=glm::cross(v.Normal,v.Tangent)*handedness;
            }else{
                auto axis=std::abs(v.Normal.y)<.95f?glm::vec3(0,1,0):glm::vec3(1,0,0);
                v.Tangent=glm::normalize(glm::cross(axis,v.Normal));v.Bitangent=glm::cross(v.Normal,v.Tangent);
            }
        }
        mesh->setGeometry(std::move(vertices),std::move(indices));
        result.triangles+=mesh->getIndices().size()/3; result.meshes.push_back(mesh);
    }
    const float scale=height/(result.high.y-result.low.y);
    const auto origin=glm::vec3((result.low.x+result.high.x)/2,result.low.y,(result.low.z+result.high.z)/2);
    result.sourceOrigin=origin;result.sourceScale=scale;
    std::cout<<"Source bounds: "<<result.low.x<<","<<result.low.y<<","<<result.low.z<<" to "<<result.high.x<<","<<result.high.y<<","<<result.high.z<<std::endl;
    for (const auto& mesh:result.meshes) {auto vertices=mesh->getVertices();for(auto& v:vertices)v.Position=(v.Position-origin)*scale;mesh->setGeometry(std::move(vertices),mesh->getIndices());}
    result.low=(result.low-origin)*scale; result.high=(result.high-origin)*scale;
    std::cout<<"Imported "<<result.meshes.size()<<" meshes, "<<result.triangles<<" triangles; height normalized to "<<height<<std::endl;
    return result;
}
