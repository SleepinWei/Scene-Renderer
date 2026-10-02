#version 430 core
out vec4 FragColor;
uniform vec3 emissionColor;
void main() {
    FragColor = vec4(emissionColor, 1.0);
}
