struct ClusterNode {vec4 lowError;vec4 high;uvec4 links;};
struct GeometryCluster {vec4 low;vec4 high;uvec4 range;};
layout(set=0,binding=0,std140) uniform ClusterParameters {
    mat4 clusterVP;mat4 clusterView;
    vec4 clusterProjection; // focal pixels, model scale bound, near plane, pixel budget.
    vec4 clusterGuard; // clip XY margins, orthographic, allow LOD.
    uvec4 clusterCounts; // clusters, mark groups, source indices, frustum enabled.
    vec4 clusterRounding; // Two-stage model/VP float cancellation bound.
};
