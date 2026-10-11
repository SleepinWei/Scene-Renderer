#include "PT/ScenePackage.h"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <limits>
#include <chrono>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
namespace {
using Json=nlohmann::json;
void check(bool v,const char *why){if(!v)throw std::runtime_error(why);}
Json matrix(glm::mat4 m){auto a=Json::array();for(int c=0;c<4;++c)for(int r=0;r<4;++r)a.push_back(m[c][r]);return a;}
void save(const std::filesystem::path &p,const Json &j){std::ofstream(p)<<j.dump();}
void mesh(const std::filesystem::path &p,bool nan=false){float a[]={-1,-1,0,0,0,1,0,0,1,-1,0,0,0,1,1,0,0,1,0,0,0,1,.5f,1};if(nan)a[0]=std::numeric_limits<float>::quiet_NaN();std::ofstream f(p,std::ios::binary);f.write("PTMESH01",8);f.write(reinterpret_cast<char*>(a),sizeof(a));}
}
int main(){try{
    auto root=std::filesystem::temp_directory_path()/("scene-renderer-pt-package-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);auto file=root/"scene.json";mesh(root/"geometry.bin");
    Json j={{"format","SceneRenderer.PT.v1"},{"geometry","geometry.bin"},{"camera",{{"world",matrix(glm::inverse(glm::lookAt(glm::vec3(0,0,2),glm::vec3(0),glm::vec3(0,1,0))))},{"projection",matrix(glm::perspective(glm::radians(50.f),1.f,.1f,100.f))},{"aspect",1}}},{"materials",Json::array({{{"base_color",{.5f,.8f,.2f}},{"roughness",.5f},{"metallic",0}}})},{"draws",Json::array({{{"offset",8},{"vertices",3},{"material",0},{"model",matrix(glm::mat4(1))}},{{"offset",8},{"vertices",3},{"material",0},{"model",matrix(glm::translate(glm::mat4(1),glm::vec3(4,0,0)))}}})}};
    save(file,j);auto package=pt::loadScenePackage(file.string(),16,8);check(package.snapshot.draws[0].mesh==package.snapshot.draws[1].mesh,"Package instances lost shared geometry");pt::CpuScene scene(package.snapshot);glm::vec3 o,d;scene.cameraRay(.5f,.5f,o,d);check(glm::length(o-glm::vec3(0,0,2))<1e-6&&glm::length(d-glm::vec3(0,0,-1))<1e-5,"Camera axes/depth conversion changed");pt::Surface hit;check(scene.intersect(o,d,0,100,hit)&&std::abs(hit.albedo.y-std::pow(.8f,2.2f))<1e-6,"Imported material or triangle changed");
    check(package.snapshot.draws[0].mesh->id&&package.snapshot.draws[0].material->id&&package.snapshot.draws[0].material==package.snapshot.draws[1].material,"Package resource IDs were zero or instance material sharing was lost");
    auto reloaded=pt::loadScenePackage(file.string(),16,8);check(reloaded.snapshot.draws[0].mesh->id!=package.snapshot.draws[0].mesh->id&&reloaded.snapshot.draws[0].material->id!=package.snapshot.draws[0].material->id,"Distinct package loads collided in the Raster resource namespace");
    auto wide=package.snapshot.frame.viewProjection;check(std::abs(wide[0][0]*2-j["camera"]["projection"][0].get<float>())<1e-5,"Camera aspect adjustment changed vertical FOV");
    auto mirror=j;mirror["draws"][0]["model"]=matrix(glm::scale(glm::mat4(1),glm::vec3(-1,1,1)));save(file,mirror);auto reversed=pt::loadScenePackage(file.string(),8,8);pt::CpuScene flipped(reversed.snapshot);check(flipped.intersect(o,d,0,100,hit)&&hit.frontFace,"Negative determinant did not preserve outward winding");
    check(reversed.snapshot.draws[0].mesh==reversed.snapshot.draws[1].mesh&&flipped.accelerationStats().uniqueMeshes==1&&flipped.accelerationStats().instances==2,"Mirrored package instance duplicated shared BLAS geometry");
    {float t[]={1,0,0,-1,1,0,0,-1,1,0,0,-1};std::ofstream f(root/"tangents.bin",std::ios::binary);f.write("PTTANG01",8);f.write(reinterpret_cast<const char*>(t),sizeof(t));}
    auto tangent=j;tangent["tangents"]="tangents.bin";for(auto &d:tangent["draws"])d["tangents_offset"]=8;save(file,tangent);auto tangentPackage=pt::loadScenePackage(file.string(),8,8);check(tangentPackage.snapshot.draws[0].mesh==tangentPackage.snapshot.draws[1].mesh&&tangentPackage.snapshot.draws[0].mesh->pathTracingTangents.size()==3,"Tangent sidecar lost shared geometry");pt::CpuScene tangentScene(tangentPackage.snapshot);check(tangentScene.exportData().geometry->vertices[0].tangent.w==-1,"Tangent handedness lost in GPU export");
    auto leaf=j;leaf["materials"][0]["bsdf_model"]="thin_diffuse";leaf["materials"][0]["diffuse_transmission"]={.6,.2,.1};save(file,leaf);auto leafPackage=pt::loadScenePackage(file.string(),8,8);pt::CpuScene leafScene(leafPackage.snapshot);check(leafScene.intersect(o,d,0,100,hit)&&hit.bsdfModel==3&&hit.diffuseTransmission==glm::vec3(.6f,.2f,.1f)&&hit.mediumId==0,"Thin diffuse material import lost transmission or added a medium");
    unsigned char pixels[]={255,0,0,255,0,255,0,255};stbi_write_png((root/"color.png").string().c_str(),2,1,4,pixels,8);auto textured=j;textured["materials"][0]["base_texture"]="color.png";save(file,textured);auto texture=pt::loadScenePackage(file.string(),8,8);check(texture.snapshot.draws[0].material->images[0]->pixels[0]==255,"Texture color/row convention changed");
    unsigned char ormPixels[]={255,64,128,255};stbi_write_png((root/"orm.png").string().c_str(),1,1,4,ormPixels,4);auto orm=j;orm["materials"][0]["orm_texture"]="orm.png";save(file,orm);auto linear=pt::loadScenePackage(file.string(),8,8);pt::CpuScene scalarScene(linear.snapshot);check(scalarScene.intersect(o,d,0,100,hit)&&std::abs(hit.roughness-64.f/255)<1e-6&&std::abs(hit.metallic-128.f/255)<1e-6,"ORM scalar channels were gamma transformed or multiplied twice");
    auto thin=j;thin["materials"][0]["thin_dielectric"]=true;thin["materials"][0]["ior"]=1.5;save(file,thin);auto sheet=pt::loadScenePackage(file.string(),8,8);pt::CpuScene thinScene(sheet.snapshot);check(thinScene.intersect(o,d,0,100,hit)&&hit.thinDielectric&&hit.mediumId==0,"Scene package thin sheet entered medium stack");
    unsigned char normalPixels[]={166,179,242,255};stbi_write_png((root/"normal.png").string().c_str(),1,1,4,normalPixels,4);
    auto water=orm;water["materials"][0].update({{"normal_texture","normal.png"},{"ior",1.333},{"bounded_volume",true},{"absorption",{.12,.035,.015}},{"scattering",{.001,.002,.003}},{"anisotropy",.2}});save(file,water);
    auto pool=pt::loadScenePackage(file.string(),8,8);pt::CpuScene poolScene(pool.snapshot);pt::Surface front,back;
    check(poolScene.intersect({0,0,2},{0,0,-1},0,100,front)&&poolScene.intersect({0,0,-2},{0,0,1},0,100,back)&&glm::length(front.normal+back.normal)<1e-6,"Tangent normal exit boundary did not reverse the full vector");
    check(front.transmissionRoughness==64.f/255&&(uint32_t(poolScene.exportData().materials[0].optics.w)&3u)==3u&&front.ior==1.333f&&front.absorption==glm::vec3(.12f,.035f,.015f)&&pool.snapshot.draws[0].pathTracingKind==4&&pool.snapshot.draws[0].pathTracingScattering==glm::vec3(.001f,.002f,.003f),"Bounded water medium import lost optical coefficients");
    float environment[]={1,2,3,4,5,6};{std::ofstream f(root/"environment.bin",std::ios::binary);f.write(reinterpret_cast<char*>(environment),sizeof(environment));}auto hdr=j;hdr["environment"]={{"width",2},{"height",1},{"file","environment.bin"}};save(file,hdr);auto sky=pt::loadScenePackage(file.string(),8,8);check(sky.environment&&sky.environment->pixels()[1]==glm::vec3(4,5,6),"HDR floats were display transformed");
    for(auto model:{"lambert","ggx_add"}){auto controlled=thin;controlled["materials"][0]["bsdf_model"]=model;save(file,controlled);auto p=pt::loadScenePackage(file.string(),8,8);pt::CpuScene c(p.snapshot);check(c.intersect(o,d,0,100,hit)&&hit.bsdfModel==(std::string(model)=="lambert"?1u:2u)&&hit.thinDielectric,"Controlled closure or thin flag lost during package load");}
    for(const auto& model:{"hair","cloth"}){auto fibers=j;fibers["materials"][0]["bsdf_model"]=model;fibers["materials"][0]["metallic"]=0;fibers["materials"][0]["sheen_weight"]=.7f;fibers["materials"][0]["hair_beta_m"]=.4f;
        save(file,fibers);auto loaded=pt::loadScenePackage(file.string(),8,8);pt::CpuScene fibersScene(loaded.snapshot);check(fibersScene.intersect(o,d,0,100,hit)&&hit.bsdfModel==(std::string(model)=="hair"?5:4)&&hit.fiber.x==.4f&&hit.sheen.x==.7f,"Fiber package import lost parameters");}
    auto reject=[&](Json bad){save(file,bad);bool rejected=false;try{pt::loadScenePackage(file.string(),8,8);}catch(const std::exception &){rejected=true;}check(rejected,"Malformed scene package accepted");};
    auto knit=j;knit["materials"][0].update({{"bsdf_model","knit"},{"metallic",0},{"knit_repeats_u",17},{"knit_repeats_v",29},{"knit_yarn_radius",.15}});save(file,knit);auto knitPackage=pt::loadScenePackage(file.string(),8,8);
    check(knitPackage.snapshot.draws[0].pathTracingBsdfModel==4&&knitPackage.snapshot.draws[0].pathTracingKnit==glm::vec4(1,17,29,.15f),"Knit material alias or parameters disappeared");
    check(knitPackage.snapshot.draws[0].pathTracingSheen.w==0,"Legacy knit unexpectedly enabled yarn reflection");
    auto yarn=knit;yarn["materials"][0].update({{"bsdf_model","yarn"},{"knit_weight",0},{"yarn_specular_weight",.65}});save(file,yarn);
    auto yarnPackage=pt::loadScenePackage(file.string(),8,8);pt::CpuScene yarnScene(yarnPackage.snapshot);
    check(yarnScene.intersect(o,d,0,100,hit)&&hit.bsdfModel==4&&hit.sheen.w==.65f&&yarnScene.exportData().materials[0].sheen.w==.65f,"Yarn reflection lost in package/CPU/GPU packing");
    auto badYarn=yarn;badYarn["materials"][0]["yarn_specular_weight"]=1.01;reject(badYarn);badYarn=yarn;badYarn["materials"][0]["yarn_specular_weight"]=-.01;reject(badYarn);
    for(auto model:{"hair","pbr","lambert"}){badYarn=yarn;badYarn["materials"][0]["bsdf_model"]=model;reject(badYarn);}
    auto invalidKnit=knit;invalidKnit["materials"][0]["knit_repeats_u"]=0;reject(invalidKnit);invalidKnit=knit;invalidKnit["materials"][0]["knit_yarn_radius"]=1;reject(invalidKnit);invalidKnit=knit;invalidKnit["materials"][0]["bsdf_model"]="hair";invalidKnit["materials"][0]["knit_weight"]=1;reject(invalidKnit);
    auto invalidFiber=j;invalidFiber["materials"][0]["bsdf_model"]="hair";invalidFiber["materials"][0]["metallic"]=0;invalidFiber["materials"][0]["hair_beta_n"]=0;reject(invalidFiber);
    auto anisotropic=j;anisotropic["materials"][0]["specular_anisotropy"]=-.8f;anisotropic["materials"][0]["specular_rotation"]=.4f;save(file,anisotropic);auto shaped=pt::loadScenePackage(file.string(),8,8);pt::CpuScene shapedScene(shaped.snapshot);check(shapedScene.intersect(o,d,0,100,hit)&&hit.specularAnisotropy==-.8f&&hit.specularRotation==.4f,"Package specular anisotropy / rotation was lost");
    auto badShape=anisotropic;badShape["materials"][0]["specular_anisotropy"]=1;reject(badShape);badShape=anisotropic;badShape["materials"][0]["specular_rotation"]=7;reject(badShape);badShape=anisotropic;badShape["materials"][0]["ior"]=1.5;reject(badShape);badShape=anisotropic;badShape["materials"][0]["bsdf_model"]="lambert";reject(badShape);
    auto unsupported=j;unsupported["materials"][0]["bsdf_model"]="unknown";reject(unsupported);
    auto invalidTangent=tangent;invalidTangent["draws"][0]["tangents_offset"]=9;reject(invalidTangent);invalidTangent=tangent;invalidTangent["draws"][0]["tangents_offset"]=56;reject(invalidTangent);invalidTangent=tangent;invalidTangent["tangents"]="../tangents.bin";reject(invalidTangent);invalidTangent=leaf;invalidTangent["materials"][0]["diffuse_transmission"]={-1,0,0};reject(invalidTangent);invalidTangent=leaf;invalidTangent["materials"][0]["ior"]=1.5;reject(invalidTangent);
    auto bad=j;bad["format"]="v2";reject(bad);bad=j;bad["geometry"]="../escape.bin";reject(bad);bad=j;bad["geometry"]=std::filesystem::absolute(root/"geometry.bin").string();reject(bad);bad=j;bad["draws"][0]["vertices"]=6;reject(bad);bad=j;bad["draws"][0]["offset"]=9;reject(bad);bad=j;bad["draws"][0]["material"]=1;reject(bad);bad=j;bad["camera"]["world"]=matrix(glm::mat4(0));reject(bad);
    mesh(root/"geometry.bin",true);reject(j);mesh(root/"geometry.bin");bad=hdr;bad["environment"]["height"]=2;reject(bad);bad=j;bad["sun"]={{"direction",{0,0,0}},{"irradiance",{1,1,1}},{"radius",.01}};reject(bad);
    bad=j;bad["materials"][0]["roughness"]=2;reject(bad);bad=j;bad["materials"][0]["ior"]=-1;reject(bad);bad=j;bad["materials"][0]["base_color"]={-1,0,0};reject(bad);
    bad=thin;bad["materials"][0]["ior"]=0;reject(bad);bad=thin;bad["materials"][0]["dielectric_roughness"]=.1;reject(bad);
    bad=water;bad["materials"][0]["absorption"]={-.1,0,0};reject(bad);bad=water;bad["materials"][0]["ior"]=0;reject(bad);bad=water;bad["materials"][0]["thin_dielectric"]=true;reject(bad);bad=water;bad["materials"][0]["normal_strength"]=-1;reject(bad);bad=water;bad["materials"][0]["anisotropy"]=1;reject(bad);
    std::filesystem::remove_all(root);std::cout<<"PT package camera, shared geometry, winding, texture/HDR and malformed buffers passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
