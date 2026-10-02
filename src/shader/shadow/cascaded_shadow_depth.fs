#version 460 core
in vec2 ShadowUV;
uniform sampler2D alphaTexture;
uniform float alphaCutoff;
void main()
{
    if (alphaCutoff > 0.0 && texture(alphaTexture,ShadowUV).a < alphaCutoff) discard;

}