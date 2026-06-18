//
// Created by A on 29.01.2026.
//

module;

#include "glad/glad.h"
#include "uv_rect.h"
#include <unordered_map>

export module atlas;

export import shader;

export
class Atlas {
private:
    unsigned int atlas_id_;        // ID текстуры цвета (Albedo)
    unsigned int normal_atlas_id_; // ID текстуры нормалей
    int width_;
    int height_;
    std::unordered_map<unsigned int, uv_rect> uv_map_;

public:
    Atlas() : atlas_id_(0), normal_atlas_id_(0), width_(0), height_(0) {}

    Atlas(unsigned int atlas_id, unsigned int normal_id, int width, int height)
        : atlas_id_(atlas_id), normal_atlas_id_(normal_id), width_(width), height_(height) {}

    ~Atlas() {
        if (atlas_id_) glDeleteTextures(1, &atlas_id_);
        if (normal_atlas_id_) glDeleteTextures(1, &normal_atlas_id_);
    }

    unsigned int id() const { return atlas_id_; }
    unsigned int normal_id() const { return normal_atlas_id_; }
    int width() const { return width_; }
    int height() const { return height_; }

    [[nodiscard]] uv_rect get_uv(unsigned int texture_handle) const {
        const auto it = uv_map_.find(texture_handle);
        if (it != uv_map_.end())
            return it->second;
        else
            return uv_map_.at(0);
    }

    void add_mapping(unsigned int texture_handle, float u0, float v0, float u1, float v1) {
        uv_map_.emplace(texture_handle, uv_rect{u0, v0, u1, v1});
    }

    void bind(const Shader* shader) const {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas_id_);
        shader->set_uniform("textureSampler", 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, normal_atlas_id_);
        shader->set_uniform("normalSampler", 1);
    }
};