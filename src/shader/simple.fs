#version 330

out vec4 FragColor;

uniform struct{
    vec3 Color; 
    vec3 Pos; 
} light; 
uniform vec3 viewPos; 

struct Object{
    vec3 FragPos;  
    vec3 Normal;  
    vec3 Color; 
};
in Object object;

void main(){
    float ambientStrength = 0.1; 
    vec3 ambient= ambientStrength * light.Color;  

    vec3 norm = normalize(object.Normal);
    vec3 lightDir = normalize(light.Pos - object.FragPos);
    float diff = max(dot(norm,lightDir),0.0);
    vec3 diffuse = diff * light.Color;

    float specularStrength = 0.5; 
    vec3 viewDir = normalize(viewPos - object.FragPos);
    vec3 halfDir = normalize(viewDir + lightDir); 
    float spec = pow(max(dot(halfDir,norm),0.0),32);
    vec3 specular = specularStrength * spec * light.Color;

    vec3 result =(ambient + diffuse + specular) * object.Color; 
    // result = clamp(result, 0.0, 1.0);
    FragColor = vec4(result,1.0);
}