#version 430 core
layout(early_fragment_tests) in;

layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gAlbedoSpec;
layout (location = 3) out vec4 gPBR;

in struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
    vec3 Tangent;
    vec3 Bitangent;
} object;

#define BottomColor vec3(0.0025, 0.02, 0.005)
#define TopColor vec3(0.1, 0.2, 0.05)
#define GrassMat vec4(0.5, 0.0, 0.0, 1.0)
#define GrassRM vec4(0.0, 0.0, 0.0, 1.0)

void main() {
	gAlbedoSpec.rgb =mix(BottomColor, TopColor, object.TexCoords.y);
    gAlbedoSpec.a = 1.0f; // is grass channel
    gPosition.rgb = object.Position;
	gNormal.rgb = object.Normal;
	// FragRoughMetal = GrassRM;
    gPBR.r = 1.0f; // roughness
    gPBR.g = 0.0f; // metallic
    gPBR.b = 1.0f; //ao
    gPBR.a = 0.0f; // height
}