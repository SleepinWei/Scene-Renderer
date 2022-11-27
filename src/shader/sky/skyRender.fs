#version 450 core

uniform sampler2D skyViewLut;
uniform samplerCube skybox;

#define PI 3.1415926
#define epsilon 0.2
float MapLatitudeToUnit(float latitude){
    return 0.5f + 0.5f * sign(latitude) * sqrt(abs(latitude)/(PI/2+epsilon));
}

out vec4 FragColor;
in vec3 viewDirection; 

void main(){
    vec3 viewDir = normalize(viewDirection);

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
    vec3 color = texture(skyViewLut,vec2(LonUnit,LatUnit)).rgb;

    // skybox
    float ratio = 4.0f;
    vec3 skyColor = pow(texture(skybox,viewDir).rgb,vec3(2.2)) / ratio;

    vec3 mixColor = color;
    float alpha = length(mixColor) / (length(mixColor) + length(skyColor));

    mixColor = mix(mixColor,skyColor,1.0f-alpha);

    FragColor = vec4(mixColor,1.0f);
    // FragColor = vec4(0.8f);
}