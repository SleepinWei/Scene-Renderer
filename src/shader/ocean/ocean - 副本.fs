#version 430 

// layout(rgba32f,binding=8) uniform sampler2D NormalRT;
// layout(rgba32f,binding=9) uniform sampler2D BubblesRT;
uniform sampler2D NormalRT;
uniform sampler2D BubblesRT;


struct DirLight 
{
    vec3 direction;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

// struct Material//材质
// {
//     vec3 ka,kd,ks;
//     float shininess;//specular衰减项
// };


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
// uniform Material material;
// uniform vec3 viewPos;
uniform DirLight dirLight;

// vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir);
vec3 lerp(vec3 a,vec3 b,float alpha);
void func();

void main()
{
    func();
}


void func()
{
    const float _FresnelScale=0.02f;
    const vec3 _OceanColorShallow=pow(vec3(0.30713776f,0.4703595f,0.5471698f),vec3(2.2f,2.2f,2.2f));
    const vec3 _OceanColorDeep=pow(vec3(0.0499288f,0.1436479f,0.20754719f),vec3(2.2f,2.2f,2.2f));
    const vec3 _BubblesColor=pow(vec3(1.0f,1.0f,1.0f),vec3(2.2f,2.2f,2.2f));
    const vec3 _Specular=pow(vec3(0.3962264f,0.3943574f,0.3943574f),vec3(2.2f,2.2f,2.2f));
    const int _Gloss=256;//256
    // const vec3 _ambient=vec3(0.1f, 0.1f, 0.1f);//material.ka
    // const vec3 _ambient=vec3(0.0, 0.0, 0.0);//material.ka
    const vec3 _ambient=pow(vec3(54, 58, 66)/256.0f,vec3(2.2f,2.2f,2.2f));//material.ka


    vec3 normal = normalize(texture(NormalRT,FragTexCoord).xyz);
    float bubbles = texture(BubblesRT,FragTexCoord).x;
    
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 lightDir = normalize(-dirLight.direction);


    //ambient
    vec3 ambient = _ambient;

    //diffuse
    float facing = clamp(dot(viewDir, normal),0.0f,1.0f);//cosθ         
    vec3 oceanColor=lerp(_OceanColorShallow,_OceanColorDeep,facing);//物体颜色，cosθ 线性插值
    vec3 oceanDiffuse = dirLight.diffuse * oceanColor * clamp(dot(lightDir, normal),0.0f,1.0f);//海洋颜色-光照
    vec3 bubblesDiffuse = dirLight.diffuse * _BubblesColor *clamp(dot(lightDir, normal),0.0f,1.0f);//泡沫颜色-光照
    vec3 cur_diffuse =lerp(oceanDiffuse,bubblesDiffuse,bubbles);
    // vec3 cur_diffuse =oceanDiffuse;
    
    // vec3 sky = vec3(1.0f,1.0f,1.0f);
    // float fresnel = clamp(_FresnelScale + (1 - _FresnelScale) * pow(1 - dot(normal, viewDir), 5),0.0f,1.0f); //菲涅尔
    // vec3 diffuse =lerp(cur_diffuse, sky, fresnel);
    vec3 diffuse =cur_diffuse;

    //specular
    vec3 halfDir = normalize(lightDir + viewDir);
    vec3 specular = dirLight.specular * _Specular * pow(max(0, dot(normal, halfDir)), _Gloss);//_Gloss或可以使用material.ks
    
    //result
    vec3 result_color = ambient + diffuse + specular;
    // vec3 result_color = diffuse;
    result_color.x=pow(result_color.x,1/2.2f);
    result_color.y=pow(result_color.y,1/2.2f);
    result_color.z=pow(result_color.z,1/2.2f);

    FragColor=vec4(result_color, 1.0f);
    // FragColor = vec4(1.0f);
}

vec3 lerp(vec3 a,vec3 b,float t)
{
    //a + (b - a) * t
    // t=normalize(t);
    return a+(b-a)*t;

//TODO:可能要选下面这个
    //a*t+b*(1-t)
    // t=normalize(t);
    // return a*t+b*(1-t);
}



// vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
// {
//     vec3 lightDir = normalize(-light.direction);
//     float diff = max(dot(normal, lightDir), 0.0);
//     vec3 reflectDir = reflect(-lightDir, normal);
//     float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
//     // vec3 ambient = light.ambient * vec3(texture(material.diffuse, FragTexCoords));
//     // vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, FragTexCoords));
//     // vec3 specular = light.specular * spec * vec3(texture(material.specular, FragTexCoords));
//     vec3 ambient = light.ambient * material.ka;
//     vec3 diffuse = light.diffuse * diff * material.kd;
//     vec3 specular = light.specular * spec * material.ks;

//     return (ambient+ diffuse+ specular);
// }