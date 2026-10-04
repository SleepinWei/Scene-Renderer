#pragma once
#include "renderer/rhi/SceneSnapshot.h"
namespace pt {
struct MeshClosure {uint32_t weldedVertices=0,removedFaces=0,filledHoles=0,boundaryEdges=0;};
// Repairs small scan holes in a detached copy; rejects non-manifold or large gaps.
MeshClosure closeSubsurfaceMesh(render::MeshPayload &mesh,float maximumHoleRadius=.08f);
}
