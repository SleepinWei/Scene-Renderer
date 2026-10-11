// Preserve the published FBX LOD0 corner attributes; no simplification.
// Build: c++ -std=c++17 tools/extract_cliff_lod0.cpp -I/opt/homebrew/include
//        -L/opt/homebrew/lib -lassimp -o build/extract_cliff_lod0
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("Usage: extract_cliff_lod0 source.fbx output.obj");
        Assimp::Importer importer;
        importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY,true);
        auto scene=importer.ReadFile(argv[1],aiProcess_Triangulate|aiProcess_PreTransformVertices);
        if(!scene||!scene->mNumMeshes)throw std::runtime_error(importer.GetErrorString());
        const aiMesh* mesh=nullptr;
        for(unsigned i=0;i<scene->mNumMeshes;++i){auto m=scene->mMeshes[i];
            std::cout<<m->mName.C_Str()<<": "<<m->mNumFaces<<" faces\n";
            if(!mesh||m->mNumFaces>mesh->mNumFaces)mesh=m;
        }
        // This is an asset-specific extractor, not a general Nanite exporter.
        // Refuse changed inputs rather than silently choosing a fallback mesh.
        if(mesh->mNumFaces!=943284||!mesh->HasNormals()||!mesh->HasTextureCoords(0))
            throw std::runtime_error("Unexpected Coastal Cliff 02 LOD0 geometry");
        std::ofstream out(argv[2]);out<<std::setprecision(std::numeric_limits<float>::max_digits10);
        out<<"# Poly Haven Coastal Cliff 02, CC0, original FBX LOD0 only\no CoastalCliff_LOD0\n";
        for(unsigned i=0;i<mesh->mNumVertices;++i){const auto& p=mesh->mVertices[i];out<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';}
        for(unsigned i=0;i<mesh->mNumVertices;++i){const auto& uv=mesh->mTextureCoords[0][i];out<<"vt "<<uv.x<<' '<<uv.y<<'\n';}
        for(unsigned i=0;i<mesh->mNumVertices;++i){const auto& n=mesh->mNormals[i];out<<"vn "<<n.x<<' '<<n.y<<' '<<n.z<<'\n';}
        for(unsigned i=0;i<mesh->mNumFaces;++i){const auto& f=mesh->mFaces[i];if(f.mNumIndices!=3)throw std::runtime_error("Nontriangle LOD0 face");
            out<<'f';for(unsigned c=0;c<3;++c){auto v=f.mIndices[c]+1;out<<' '<<v<<'/'<<v<<'/'<<v;}out<<'\n';}
        if(!out)throw std::runtime_error("LOD0 export failed");
        std::cout<<"Exported "<<mesh->mNumFaces<<" triangles; source units and attributes preserved\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
