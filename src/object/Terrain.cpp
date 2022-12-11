#include"Terrain.h"
#include"../renderer/Material.h"
#include"../component/TerrainComponent.h"
#include"../buffer/SSBO.h"
#include"../buffer/ImageTexture.h"
#include"../utils/Shader.h"
#include"../component/Grass.h"
#include<assert.h>

Terrain::Terrain() {

}

Terrain::~Terrain() {

}

/// <summary>
/// this implementation is only suitable for deferred pipeline
/// </summary>
/// <param name="shader"></param>
void Terrain::render(const std::shared_ptr<Shader>& shader) {
	assert(shader == nullptr);
	auto&& terrainComponent = std::static_pointer_cast<TerrainComponent>(GetComponent("TerrainComponent"));
	auto&& grassComponent = std::static_pointer_cast<Grass>(GetComponent("Grass"));
	if (terrainComponent && terrainComponent->initDone) {
		terrainComponent->render(terrainComponent->terrainGBuffer);
	}
	if (grassComponent && terrainComponent->initDone) {
		grassComponent->render(grassComponent->shader);
	}

}

void Terrain::constructCall() {
	// step1: terrainComponent:get terrain surface
	// step2: grassComponent: get grass data
	auto&& terrainComponent = std::static_pointer_cast<TerrainComponent>(GetComponent("TerrainComponent"));
	auto&& grassComponent = std::static_pointer_cast<Grass>(GetComponent("Grass"));
	if (terrainComponent) {
		terrainComponent->prepareData();
		if (grassComponent) {
			// if grass exist, prepare grass
			grassComponent->prepareData();
		}
		terrainComponent->constructCall();
		if (grassComponent) {
			// if grass exist, construct grass
			grassComponent->constructCall();
		}
	}
}

void Terrain::loadFromJson(json& data) {
	auto terrainComponent = std::make_shared<TerrainComponent>();
	terrainComponent->loadFromJson(data);
	addComponent(terrainComponent);
	if (data.find("grass")!=data.end()) {
		auto grassComponent = std::make_shared<Grass>();
		addComponent(grassComponent);
	}
}