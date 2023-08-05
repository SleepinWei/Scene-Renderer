#version 430 core
out float FragColor;
  
in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D texNoise;

uniform vec3 samples[64];

layout(std140,binding=0) uniform VP{
    mat4 projection;
    mat4 view;
};

uniform float SCR_HEIGHT;
uniform float SCR_WIDTH;
uniform float kernelSize;
uniform float radius; 
// tile noise texture over screen, based on screen dimensions divided by noise size

void main()
{
    //divide into 16 tiles
    vec2 noiseScale = vec2(SCR_WIDTH/4.0, SCR_HEIGHT/4.0); // screen = 800x600

    vec3 fragPos   = texture(gPosition, TexCoords).xyz;
    vec3 normal    = texture(gNormal, TexCoords).rgb;
    vec3 randomVec = texture(texNoise, fract(TexCoords * noiseScale)).xyz;  

    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);  

    float occlusion = 0.0;
    for(int i = 0; i < kernelSize; ++i)
    {
        // get sample position
        vec3 samplePos = TBN * samples[i]; // from tangent to view-space
        samplePos = fragPos + samplePos * radius; 
        
        // conver to screen space
        vec4 offset = vec4(samplePos, 1.0);
        offset      = projection * offset;    // from view to clip-space
        offset.xyz /= offset.w;               // perspective divide
        offset.xyz  = offset.xyz * 0.5 + 0.5; // transform to range 0.0 - 1.0  

        float sampleDepth = texture(gPosition, offset.xy).z; 
        const float bias = 0.025;
        // range check
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion       += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;  
    }  
    occlusion = 1.0 - (occlusion / kernelSize);
    FragColor = occlusion;  
}