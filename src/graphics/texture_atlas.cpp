//
// Created by A on 29.01.2026.
//

#include "glad/glad.h"

#include "texture_atlas.h"

#include <iostream>
#include <ostream>

#include "texture_manager.h"

texture_atlas::texture_atlas(const unsigned int &atlas_id, const int &width, const int &height)
    : atlas_id_(atlas_id), width_(width), height_(height) {}

texture_atlas::~texture_atlas() {
    if (atlas_id_) glDeleteTextures(1, &atlas_id_);
}

const texture_atlas::uv_rect& texture_atlas::get_uv(const unsigned int &texture_handle) const {
    const auto it = uv_map_.find(texture_handle);
    if (it != uv_map_.end())
        return it->second;
    else
        return uv_map_.at(texture_manager::preload_textures::ohno);
}

void texture_atlas::add_mapping(
    unsigned int &texture_handle,
    const float &u0,
    const float &v0,
    const float &u1,
    const float &v1)
{
    uv_map_.emplace(texture_handle, uv_rect{u0, v0, u1, v1});
}

void texture_atlas::bind() const {
    glBindTexture(GL_TEXTURE_2D, atlas_id_);
}
