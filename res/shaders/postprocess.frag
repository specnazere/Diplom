#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D screenTexture;
uniform vec2 resolution;

void main() {
    FragColor = texture(screenTexture, vUV);
    // Добавьте эффекты здесь:
    // - яркость/контраст
    // - гамма-коррекция
    // - bloom, размытие и т.д.
}