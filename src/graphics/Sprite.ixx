//
// Created by A on 27.01.2026.
//

module;

#include "glm/fwd.hpp"

#include "uv_rect.h"

#include <string>

export module sprite;

export
struct alignas(16) SpriteData {
    glm::float32 width{};
    glm::float32 height{};
    glm::float32 angle{};
    glm::float32 position_x{};
    glm::float32 position_y{};
    glm::float32 paralax = 1.0f;
    uv_rect uvRect;
};

export
struct SpriteInitData {
    glm::float32 width{};
    glm::float32 height{};
    glm::float32 angle{};
    glm::float32 position_x{};
    glm::float32 position_y{};
    glm::float32 paralax = 1.0f;
};

export
class Sprite {
public:
    // Индекс в векторе sprite_data_ репозитория. -1 если не активен или не добавлен
    SpriteData* data = nullptr;
    float depth = 0.0f;
    bool active = false;
    size_t textureDesc = 0;

    Sprite() {}

    // Конструктор для удобства создания
    Sprite(const size_t& texture) :
        textureDesc(texture) {}
};