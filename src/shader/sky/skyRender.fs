#version 450 core

// layout(rgba32f,binding=0) uniform image2D transmittance_texture;
// layout(rgba32f,binding=0) uniform image2D skyViewLut;
uniform sampler2D transmittance_texture;
uniform sampler2D skyViewLut;

#define PI 3.1415926
float MapLatitudeToUnit(float latitude){
    return 0.5f + 0.5f * sign(latitude) * sqrt(abs(latitude)/(PI/2));
}

float MapUnitToLatitude(float unit){
    float s = sign(unit - 0.5f);
    return ((2 * unit-1.0f) * (2*unit-1.0f)) * s * PI / 2;
}

out vec4 FragColor;
in vec3 viewDirection; 

void main(){
    vec3 viewDir = normalize(viewDirection);
    float sinLat = length(cross(viewDir,vec3(0.0f, 1.0f, 0.0f)));
    vec3 projected = vec3(viewDir.x,0.0f,viewDir.z);
    float cosLon = dot(normalize((projected)),
        vec3(0.0f,0.0f,-1.0f));
    float Lat = asin(sinLat);
    float Lon = acos(cosLon);
    float LatUnit = MapLatitudeToUnit(Lat);
    float LonUnit = Lon / (PI);
    vec4 color = texture(skyViewLut,vec2(LonUnit,LatUnit));
    FragColor = color;
}