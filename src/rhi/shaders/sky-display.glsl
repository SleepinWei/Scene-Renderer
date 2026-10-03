// The disk is excluded from environment LUTs; directional BRDF lighting accounts for its energy.
vec3 displayedSky(vec3 direction) {
    vec3 sky=sampleSkyLut(skyRadianceLut,direction);
    vec3 sun=normalize(sunDirectionRadius.xyz);float dotSun=clamp(dot(direction,sun),-1.0,1.0);
    float angle=atan(length(cross(direction,sun)),dotSun);
    float width=max(fwidth(angle),1e-6),radius=sunDirectionRadius.w;
    float disk=1.0-smoothstep(radius-width*.5,radius+width*.5,angle);
    float horizonWidth=max(fwidth(direction.y),1e-6);
    float earthVisibility=smoothstep(skySettings.z-horizonWidth*.5,skySettings.z+horizonWidth*.5,direction.y);
    // The scene HDR target is RGBA16F; prevent overflow while retaining an HDR solar disk.
    return min(sky+sunRadiance.rgb*disk*earthVisibility*skySettings.w,vec3(65000));
}
