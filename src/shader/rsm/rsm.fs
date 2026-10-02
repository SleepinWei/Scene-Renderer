#version 430 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D normalMap;
uniform sampler2D worldPosMap;
uniform sampler2D fluxMap;
uniform sampler2D randomMap;
uniform sampler2D inTexture;
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gPBR;
uniform mat4 lightSpaceMatrix;
uniform int sample_num;
uniform float sample_radius;
uniform float rsmIntensity;
uniform float minDistance;
uniform int indirectOnly;
const float PI = 3.14159265359;

vec3 indirectRadiance(vec3 position, vec3 N) {
    vec4 clip = lightSpaceMatrix * vec4(position, 1);
    if (clip.w <= 0.0 || rsmIntensity <= 0.0) return vec3(0);
    vec2 center = clip.xy / clip.w * 0.5 + 0.5;
    ivec2 size = textureSize(fluxMap, 0);
    vec3 irradiance = vec3(0);
    for (int i = 0; i < sample_num; ++i) {
        vec2 uv = center + texelFetch(randomMap, ivec2(i, 0), 0).xy * sample_radius;
        // Out-of-map samples count as zero; clamping duplicates edge VPLs.
        if (any(lessThan(uv, vec2(0))) || any(greaterThanEqual(uv, vec2(1)))) continue;
        ivec2 texel = ivec2(uv * vec2(size));
        vec4 vplPosition = texelFetch(worldPosMap, texel, 0);
        if (vplPosition.w < 0.5) continue;
        vec3 sourceNormal = normalize(texelFetch(normalMap, texel, 0).xyz);
        vec3 delta = position - vplPosition.xyz;
        float distance2 = dot(delta, delta);
        if (distance2 < 1e-8) continue;
        vec3 direction = delta * inversesqrt(distance2);
        float geometry = max(dot(sourceNormal, direction), 0.0) * max(dot(N, -direction), 0.0);
        geometry /= max(distance2, minDistance * minDistance);
        // Stored flux is total reflected power per texel; Lambertian emission is Phi / PI.
        irradiance += texelFetch(fluxMap, texel, 0).rgb * geometry / PI;
    }
    // Uniform-disk PDF in UV: 1 / (PI R^2). Sum all represented texels, not their mean.
    float representedTexels = PI * sample_radius * sample_radius * float(size.x * size.y);
    irradiance *= representedTexels / float(sample_num);
    vec3 albedo = texture(gAlbedoSpec, TexCoords).rgb;
    float metallic = clamp(texture(gPBR, TexCoords).g, 0.0, 1.0);
    return rsmIntensity * irradiance * albedo * (1.0 - metallic) / PI;
}
void main() {
    vec3 base = texture(inTexture, TexCoords).rgb;
    vec3 normal = texture(gNormal, TexCoords).xyz;
    vec3 indirect = vec3(0);
    // Cleared G-buffer normals are zero: preserve sky and forward-only pixels.
    if (dot(normal, normal) > 0.25)
        indirect = indirectRadiance(texture(gPosition, TexCoords).xyz, normalize(normal));
    FragColor = vec4(indirectOnly != 0 ? indirect : base + indirect, 1);
}
