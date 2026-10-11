layout(set=0,binding=0,std140) uniform CullParameters {mat4 clip;vec4 minimum;vec4 maximum;vec4 guard;};
layout(set=1,binding=0,std430) readonly buffer InputArgs {uint inputArgs[];};
layout(set=1,binding=1,std430) readonly buffer InputPoses {mat4 inputPoses[];};
layout(set=1,binding=2,std430) buffer OutputArgs {uint outputArgs[];};
layout(set=1,binding=3,std430) buffer OutputPoses {mat4 outputPoses[];};
