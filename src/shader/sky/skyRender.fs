#version 450 core

uniform sampler2D skyViewLut;

#define PI 3.1415926
#define epsilon 0.2
float MapLatitudeToUnit(float latitude){
    return 0.5f + 0.5f * sign(latitude) * sqrt(abs(latitude)/(PI/2+epsilon));
}

out vec4 FragColor;
in vec3 viewDirection; 

void main(){
    vec3 viewDir = normalize(viewDirection);
    // float sinLat = length(cross(viewDir,vec3(0.0f, 1.0f, 0.0f)));
    float sinLat = viewDir.y;
    float tanLon = -abs(viewDir.x) / viewDir.z; 
    float Lat = asin(sinLat);
    float Lon = atan(tanLon);
    if(Lon < 0.0f)
        Lon = PI + Lon;
    if(viewDir.x < 0.0f)
        Lon = 2 * PI - Lon;
    float LatUnit = MapLatitudeToUnit(Lat);
    float LonUnit = Lon / (2 * PI);
    vec4 color = texture(skyViewLut,vec2(LonUnit,LatUnit));
    // vec4 color = vec4(LatUnit,LonUnit,1.0f,1.0f);
    FragColor = color;
    // FragColor = vec4(0.8f);
}