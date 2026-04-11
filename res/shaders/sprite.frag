#version 330 core

in vec2  vUV;
out vec4 FragColor;

uniform sampler2D textureSampler;

void main() {
    vec4 texColor = texture(textureSampler, vUV);
    if (texColor.a <= 0) discard;  // отсечение прозрачных пикселей
    FragColor = texColor;
}