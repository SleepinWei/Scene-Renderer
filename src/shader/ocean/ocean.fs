#version 430 
#define PI 3.1415926f

// layout(rgba32f,binding=8) uniform sampler2D NormalRT;
// layout(rgba32f,binding=9) uniform sampler2D BubblesRT;
uniform sampler2D NormalRT;
uniform sampler2D BubblesRT;
// uniform sampler2D SkyView;
uniform sampler2D skyview;

struct DirLight 
{
    vec3 direction;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};


in vec3 FragPos;
in vec3 FragNormal;
in vec2 FragTexCoord;

out vec4 FragColor;
layout(std140,binding=0) uniform VP
{
    mat4 projection;
    mat4 view;
    vec3 viewPos;
};

uniform DirLight dirLight;
uniform float outer_FresnelScale;
uniform vec3 outer_OceanColorShallow;
uniform vec3 outer_OceanColorDeep;
uniform vec3 outer_BubblesColor;
uniform vec3 outer_Specular;
uniform int outer_Gloss;
uniform vec3 outer_ambient;

vec3 lerp(vec3 a,vec3 b,float alpha);
void func();

void main()
{
    func();
}

#define epsilon 0.2f
float MapLatitudeToUnit(float latitude){
    return 0.5f + 0.5f * sign(latitude) * sqrt(abs(latitude)/(PI/2+epsilon));
}

void func()
{
    float _FresnelScale=outer_FresnelScale;
    vec3 _OceanColorShallow=outer_OceanColorShallow;
    vec3 _OceanColorDeep=outer_OceanColorDeep;
    vec3 _BubblesColor=outer_BubblesColor;
    vec3 _Specular=outer_Specular;
    int _Gloss=outer_Gloss;
    vec3 _ambient=outer_ambient;
    // float _FresnelScale=0.02f;
    // vec3 _OceanColorShallow=pow(vec3(0.30713776f,0.4703595f,0.5471698f),vec3(2.2f,2.2f,2.2f));
    // vec3 _OceanColorDeep=pow(vec3(0.0499288f,0.1436479f,0.20754719f),vec3(2.2f,2.2f,2.2f));
    // vec3 _BubblesColor=pow(vec3(1.0f,1.0f,1.0f),vec3(2.2f,2.2f,2.2f));
    // vec3 _Specular=pow(vec3(0.3962264f,0.3943574f,0.3943574f),vec3(2.2f,2.2f,2.2f));
    // int _Gloss=256;//256
    // vec3 _ambient=pow(vec3(54, 58, 66)/256.0f,vec3(2.2f,2.2f,2.2f));//material.ka

    vec3 normal = normalize(texture(NormalRT,FragTexCoord).xyz);
    float bubbles = texture(BubblesRT,FragTexCoord).x;
    
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 lightDir = normalize(-dirLight.direction);


    //ambient
    vec3 ambient = _ambient;

    //diffuse
    float facing = clamp(dot(viewDir, normal),0.0f,1.0f);//cosθ         
    vec3 oceanColor=lerp(_OceanColorShallow,_OceanColorDeep,facing);//物体颜色，cosθ 线性插值
    vec3 oceanDiffuse = dirLight.diffuse * oceanColor * clamp(pow(dot(lightDir, normal), 20.0f),0.0f,1.0f);//海洋颜色-光照
    vec3 bubblesDiffuse = dirLight.diffuse * _BubblesColor *clamp(dot(lightDir, normal),0.0f,1.0f);//泡沫颜色-光照
    vec3 cur_diffuse =lerp(oceanDiffuse,bubblesDiffuse,bubbles);
    // vec3 cur_diffuse =oceanDiffuse;

    vec3 reflectDir = normalize(reflect(-viewDir, normal));
    float sinLat = abs(reflectDir.y);
    float tanLon = -abs(reflectDir.x) / reflectDir.z; 
    float Lat = asin(sinLat);
    float Lon = atan(tanLon);
    if(Lon < 0.0f)
        Lon = PI + Lon;
    if(reflectDir.x < 0.0f)
        Lon = 2 * PI - Lon;
    float LatUnit = MapLatitudeToUnit(Lat);
    float LonUnit = Lon / (2 * PI);

    vec3 sky = pow(texture(skyview,vec2(LonUnit,LatUnit)).xyz,vec3(2.2));

    float fresnel = clamp(_FresnelScale + (1 - _FresnelScale) * pow(1 - dot(normal, viewDir), 5),0.0f,1.0f); //菲涅尔
    vec3 diffuse =lerp(cur_diffuse, sky, fresnel);
    // vec3 diffuse =sky;

    //specular
    vec3 halfDir = normalize(lightDir + viewDir);
    vec3 specular = dirLight.specular * _Specular * pow(max(0, dot(normal, halfDir)), _Gloss);//_Gloss或可以使用material.ks
    // vec3 specular = vec3(0.0f);
    
    //result
    vec3 result_color = ambient + diffuse + specular;
    // vec3 result_color = diffuse;
    // result_color.x=pow(result_color.x,1/2.2f);
    // result_color.y=pow(result_color.y,1/2.2f);
    // result_color.z=pow(result_color.z,1/2.2f);
    result_color = clamp(result_color, 0.0f, 1.0f);

    FragColor=vec4(result_color, clamp(2.0f * fresnel + 0.4f, 0.0f, 1.0f));
    // FragColor = vec4(1.0f);
}

vec3 lerp(vec3 a,vec3 b,float t)
{
    //a + (b - a) * t
    return a+(b-a)*t;
}
