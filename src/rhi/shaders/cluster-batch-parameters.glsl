struct ClusterNode {vec4 lowError;vec4 high;uvec4 links;};
struct GeometryCluster {vec4 low;vec4 high;uvec4 range;};
struct ClusterMesh {
    mat4 vp;mat4 view;vec4 projection;vec4 guard;uvec4 counts;vec4 rounding;uvec4 ranges;mat4 historyVP;vec4 historyRounding;
};
layout(set=0,binding=0,std140) uniform ClusterBatchParameters {uvec4 batchCounts;uvec4 batchStage;}; // meshes, width, height, HiZ levels. Stage: 0 cut, 1 same-frame, 2 early, 3 late.
layout(set=1,binding=7,std430) readonly buffer ClusterViews {ClusterMesh meshes[];};
