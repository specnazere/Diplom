//
// Created by A on 29.01.2026.
//

module;

#include "glad/glad.h"
#include "uv_rect.h"
#include <unordered_map>

export module atlas;

import shader;

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

    Atlas(const unsigned int &atlas_id, const unsigned int &normal_id, const int &width, const int &height)
        : atlas_id_(atlas_id), normal_atlas_id_(normal_id), width_(width), height_(height) {}

    ~Atlas() {
        if (atlas_id_) glDeleteTextures(1, &atlas_id_);
        if (normal_atlas_id_) glDeleteTextures(1, &normal_atlas_id_);
    }

    // Геттеры
    unsigned int id() const { return atlas_id_; }
    unsigned int normal_id() const { return normal_atlas_id_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Получить UV для исходной текстуры по её хендлу
    // Возвращает те же координаты для обеих текстур, так как они совпадают
    const uv_rect& get_uv(const unsigned int &texture_handle) const {
        const auto it = uv_map_.find(texture_handle);
        if (it != uv_map_.end())
            return it->second;
        else
            return uv_map_.at(0); // first always placeholder
    }

    // Добавить маппинг хендл → UV
    // Этот маппинг применяется сразу к обеим текстурам атласа
    void add_mapping(unsigned int &texture_handle, const float &u0, const float &v0, const float &u1, const float &v1)
    {
        uv_map_.emplace(texture_handle, uv_rect{u0, v0, u1, v1});
    }

    // Привязка текстуры цвета
    void bind(const Shader* shader) const {
        // 1. Настройка Текстурного Юнита 0 (Цвет)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas_id_);
        // Сообщаем шейдеру: сэмплер "textureSampler" читает из юнита 0
        shader->set_uniform("textureSampler", 0);

        // 2. Настройка Текстурного Юнита 1 (Нормали)
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, normal_atlas_id_);
        // Сообщаем шейдеру: сэмплер "normalSampler" читает из юнита 1
        shader->set_uniform("normalSampler", 1);
    }
};