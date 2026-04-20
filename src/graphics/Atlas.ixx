//
// Created by A on 29.01.2026.
//

module;

#include "glad/glad.h"

#include "uv_rect.h"

#include <unordered_map>

export module atlas;

export
class Atlas {
private:
    unsigned int atlas_id_;
    int width_;
    int height_;
    std::unordered_map<unsigned int, uv_rect> uv_map_;

public:
    Atlas() {}

    Atlas(const unsigned int &atlas_id, const int &width, const int &height)
    : atlas_id_(atlas_id), width_(width), height_(height) {}
    ~Atlas() {
        if (atlas_id_) glDeleteTextures(1, &atlas_id_);
    }

    unsigned int id() const { return atlas_id_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Получить UV для исходной текстуры по её хендлу
    const uv_rect& get_uv(const unsigned int &texture_handle) const {
        const auto it = uv_map_.find(texture_handle);
        if (it != uv_map_.end())
            return it->second;
        else
            return uv_map_.at(0); // first always placeholder
    }

    // Добавить маппинг хендл → UV (вызывается только из TextureManager)
    void add_mapping(unsigned int &texture_handle, const float &u0, const float &v0, const float &u1, const float &v1)
    {
        uv_map_.emplace(texture_handle, uv_rect{u0, v0, u1, v1});
    }

    void bind() const {
        glBindTexture(GL_TEXTURE_2D, atlas_id_);
    }
};
