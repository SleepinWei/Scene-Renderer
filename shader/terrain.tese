#version 410 core
layout(quads, fractional_odd_spacing, ccw) in;

uniform sampler2D heightMap;
uniform sampler2D normalMap;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float yScale;
uniform float yShift;

// out vec3 FragPos;
// out vec3 Normal;
out struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
} object;

in vec2 TextureCoord[];

void main()
{
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;
    object.TexCoords = vec2(u,v);

    vec2 t00 = TextureCoord[0];
    vec2 t01 = TextureCoord[1];
    vec2 t10 = TextureCoord[2];
    vec2 t11 = TextureCoord[3];

    vec2 t0 = (t01 - t00) * u + t00;
    vec2 t1 = (t11 - t10) * u + t10;
    vec2 texCoord = (t1 - t0) * v + t0;

    float Height = texture(heightMap, texCoord).y * yScale - yShift;
    vec3 tNorm= texture(normalMap,texCoord).xyz * 2 - 1.0f;
    // Normal = tNorm;
    tNorm.y = tNorm.y * 256 / yScale; 
    // color = texture(heightMap,texCoord).y;

    vec4 p00 = gl_in[0].gl_Position;
    vec4 p01 = gl_in[1].gl_Position;
    vec4 p10 = gl_in[2].gl_Position;
    vec4 p11 = gl_in[3].gl_Position;

    vec4 uVec = p01 - p00;
    vec4 vVec = p10 - p00;
    vec4 normal = normalize( vec4(cross(vVec.xyz, uVec.xyz), 0) );

    vec4 p0 = (p01 - p00) * u + p00;
    vec4 p1 = (p11 - p10) * u + p10;
    vec4 p = (p1 - p0) * v + p0 + normal * Height;

    gl_Position = projection * view * model * p;
    tNorm.x = tNorm.x / model[0][0];
    tNorm.z = tNorm.z / model[2][2];
    object.Normal = normalize(tNorm);
    object.Position = vec3(model * p);
    // object.TexCoords = texCoord;
}