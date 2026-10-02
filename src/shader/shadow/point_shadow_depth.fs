#version 460 core
in vec2 ShadowUV;
uniform sampler2D alphaTexture;
uniform float alphaCutoff;
in vec4 FragPos ;

uniform vec3 lightPos;
uniform float far_plane;

void main()

{
    if (alphaCutoff > 0.0 && texture(alphaTexture,ShadowUV).a < alphaCutoff) discard;
	float lightDistance =length(FragPos.xyz-lightPos);
	lightDistance =lightDistance /far_plane;
	gl_FragDepth=lightDistance;
}
