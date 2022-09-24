#version 410 core 

in vec3 FragPos;
in vec3 Normal;
uniform struct{
    vec3 Color; 
    vec3 Pos; 
} light; 
uniform vec3 viewPos; 

out vec4 FragColor;

void main(){
    vec3 objectColor = vec3(0.1,1.0,0.0);
    vec3 Q1 = dFdx(FragPos);
    vec3 Q2 = dFdy(FragPos);
    // vec3 norm = normalize(cross(Q1,Q2));
    vec3 norm = Normal;

    float ambientStrength = 0.1; 
    vec3 ambient= ambientStrength * light.Color;  

    vec3 lightDir = normalize(light.Pos - FragPos);
    float diff = max(dot(norm,lightDir),0.0);
    vec3 diffuse = diff * light.Color;

    float specularStrength = 0.01; 
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfDir = normalize(viewDir + lightDir); 
    float spec = pow(max(dot(halfDir,norm),0.0),32);
    vec3 specular = specularStrength * spec * light.Color;

    vec3 result =(ambient + diffuse + specular) * objectColor; 
    FragColor = vec4(result, 1.0);
}