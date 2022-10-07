#version 330

out vec4 FragColor;

void main(){
    // FragColor = projection * view * vec4(object.Position,1.0);
    // FragColor = vec4(texture(material.albedo,object.TexCoords));
    FragColor = vec4(1.0f);
}