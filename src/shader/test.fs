#version 330

out vec4 FragColor;

uniform sampler2D tex;
in vec2 texCoords;

void main(){
    // FragColor = projection * view * vec4(object.Position,1.0);
    // FragColor = vec4(texture(material.albedo,object.TexCoords));
    FragColor = texture(tex,texCoords);
}