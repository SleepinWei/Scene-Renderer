#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0, binding=0, std140) uniform Frame { vec4 tint; };
layout(set=1, binding=1) uniform sampler2D albedo;
void main() { color = texture(albedo, uv) * tint; }
