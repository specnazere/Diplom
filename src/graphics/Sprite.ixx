//
// Created by A on 27.01.2026.
//
module;

#include "glm/fwd.hpp"
#include "uv_rect.h"

export module sprite;

export
struct alignas(16) SpriteData {
    glm::float32 width = 100;
    glm::float32 height = 100;

    // Локальные параметры спрайта (применяются ПОСЛЕ глобальной трансформации)
    glm::float32 angle{};
    glm::vec2 position{};

    glm::float32 paralax = 1.0f;
    glm::float32 depth = 0.0f;
    uv_rect uvRect;
    glm::u8vec4 color;

    unsigned int transformIdx = 0xFFFFFFFFu;
};

export
class Sprite {
public:
    size_t dataIdx = ~size_t{0};
    size_t transformIdx = ~size_t{0};

    float depth = 0.0f;
    size_t textureDesc = 0;

    Sprite() {}

    explicit Sprite(const size_t& texture) :
        textureDesc(texture) {}

    // Быстрая проверка на валидность
    inline bool isValid() const {
        return dataIdx != ~size_t{0};
    }
};