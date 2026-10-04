#pragma once
#include "PT/CpuPathTracer.h"
namespace pt {
struct ValidationScene {render::RenderWorldSnapshot snapshot;std::vector<DielectricMaterial> dielectrics;};
ValidationScene makeCausticsScene(uint32_t width,uint32_t height,bool glass=true);
ValidationScene makeDragonScene(uint32_t width,uint32_t height,const std::string &path,bool glass=true);
}
