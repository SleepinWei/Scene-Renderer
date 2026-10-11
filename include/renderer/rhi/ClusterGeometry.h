#pragma once
#include "renderer/rhi/GpuMesh.h"
#include <string>
namespace render {
struct MeshPayload;
struct ClusterBuildSettings {
    uint32_t trianglesPerCluster=128,maxLevels=12;
    float maxError=.25f,maxUvError=.03f;
    size_t byteBudget=64*1024*1024;
    bool spatialPartition=true; // False retains the original attribute-adjacency BFS for same-build A/B.
    float maxNormalError=.05f; // Accumulated endpoint deviation between unit normals; about 2.9 degrees.
    float minParentReduction=.125f; // Skip expensive parent copies with less than 12.5% fewer triangles.
};
struct ClusterSettings {
    bool enabled=false,wireframe=false;
    float pixelError=1;
    bool batched=true,occlusion=false,twoPhase=true;
    bool fusedPyramid=true,fusedLate=true,conservativeIntervals=false; // Diagnostic A/B switches; conservative results unchanged.
};
// Source coverage bounds, not just simplified geometry bounds. Parent coverage
// contains child coverage and parent error is monotone, yielding a unique cut.
struct alignas(16) ClusterNode {
    glm::vec4 lowError{0},high{0};
    glm::uvec4 links{UINT32_MAX}; // parent, child A, child B, level.
};
struct alignas(16) GeometryCluster {
    glm::vec4 low{0},high{0};
    glm::uvec4 range{0}; // first index, index count, owning group node, reserved.
};
struct ClusterHierarchy {
    std::vector<ClusterNode> nodes;
    std::vector<GeometryCluster> clusters;
    std::vector<uint32_t> indices,roots;
    uint32_t sourceIndices=0,leafClusters=0;
    double buildSeconds=0;
    bool budgetLimited=false;
    uint32_t positionVertices=0,attributeVertices=0,seamVertices=0;
    bool spatialPartition=false;
    float normalErrorBound=0;
};
// Pure CPU, immutable source; endpoint QEM contractions lock group borders and
// attribute seams. All indices still address the ORIGINAL vertex/tangent data.
std::shared_ptr<const ClusterHierarchy> buildClusterHierarchy(const MeshPayload&,const ClusterBuildSettings& = {});
std::vector<uint32_t> selectClusterNodes(const ClusterHierarchy&,const glm::mat4& viewModel,
    float focalPixels,float scale,float nearPlane,float pixelError,bool orthographic=false,bool allowLod=true);
class GpuClusterGeometry {
public:
    GpuClusterGeometry(std::shared_ptr<rhi::GraphicsDevice>,std::shared_ptr<const ClusterHierarchy>);
    const std::shared_ptr<const ClusterHierarchy> hierarchy;
    rhi::BufferHandle nodes,clusters,indices;
private:
    Resources resources_;
};
struct ClusterCullCount {size_t packet=0;uint32_t sourceTriangles=0,selectedTriangles=0,selectedClusters=0;};
struct ClusterCullStats {size_t packets=0,dispatches=0,bufferBytes=0;uint64_t sourceTriangles=0;bool batched=false,occlusion=false,twoPhase=false,historyUsed=false;};
struct ClusterVisibility {size_t packet=0;uint32_t cluster=0,indexCount=0;bool selected=false,early=false,late=false;};
void captureClusterScene(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
void validateClusterHierarchy();
void validateClusterRendering(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
