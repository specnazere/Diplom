//
// Created by A on 28.01.2026.
//

#include "texture.h"

#include "glad/glad.h"

texture::texture(const int width, const int height, std::string path, unsigned char *data)
    : width(width), height(height), path(std::move(path)), data(data) {}

texture::~texture() {
    //if (id) glDeleteTextures(1, &id);
}