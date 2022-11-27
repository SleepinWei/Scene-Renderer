#include"Terrain.h"
#include"../renderer/Material.h"
#include"../component/TerrainComponent.h"
#include"../buffer/SSBO.h"
#include"../buffer/ImageTexture.h"
#include"../utils/Shader.h"

Terrain::Terrain() {

}

Terrain::~Terrain() {

}

void Terrain::render(const std::shared_ptr<Shader>& shader) {
	auto& terrainComponent = std::static_pointer_cast<TerrainComponent>(this->GetComponent("TerrainComponent"));
	if (terrainComponent) {
		terrainComponent->render(shader);
	}
}

void Terrain::loadFromJson(json& data) {
	auto terrainComponent = std::make_shared<TerrainComponent>();
	terrainComponent->loadFromJson(data);
	addComponent(terrainComponent);
}