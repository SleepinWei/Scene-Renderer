#version 450
layout(location=0) in vec3 position;layout(location=1) in vec3 normal;layout(location=2) in vec2 texcoord;
layout(set=0,binding=0,std140) uniform MotionCamera {mat4 currentVP;mat4 previousVP;mat4 previousView;};
layout(set=0,binding=1,std140) uniform MotionObject {mat4 model;mat4 previousModel;ivec4 motionFlags;};
layout(location=2) out vec2 uv;layout(location=3) out vec4 previousClip;layout(location=4) out vec4 currentClip;layout(location=5) out float previousDepth;layout(location=6) flat out int reactive;
layout(set=0,binding=3,std430) readonly buffer OutPose {mat4 instancePoses[];};
void main(){vec4 p=model*instancePoses[gl_InstanceIndex]*vec4(position,1),old=previousModel*instancePoses[gl_InstanceIndex]*vec4(position,1);currentClip=currentVP*p;gl_Position=currentClip;previousClip=previousVP*old;previousDepth=-(previousView*old).z;uv=texcoord;reactive=motionFlags.x;}
