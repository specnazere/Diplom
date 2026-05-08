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
    glm::float32 position_x{};
    glm::float32 position_y{};

    glm::float32 paralax = 1.0f;
    glm::float32 depth = 0.0f;
    uv_rect uvRect;

    unsigned int transformIdx = 0xFFFFFFFFu;
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

// Узел трансформации для иерархии
// Хранит локальные координаты и ссылку на родителя
export
struct TransformNode {
    glm::vec2 position = glm::vec2(0, 0);
    glm::float32 angle = 0.0f;
    int parentIndex = -1; // -1 если нет родителя

    // Для GPU удобно иметь выравнивание, например, до 16 байт
    // float padding;
};

export
class Sprite {
public:
    // Индекс в векторе sprite_data_ репозитория. ~0 если слот свободен
    size_t dataIdx = ~size_t{0};

    // Индекс в векторе transforms_ репозитория. ~0 если трансформация не назначена
    size_t transformIdx = ~size_t{0};

    float depth = 0.0f;
    // bool active удален. Активность определяется как (dataIdx != ~size_t{0})
    size_t textureDesc = 0;

    Sprite() {}

    explicit Sprite(const size_t& texture) :
        textureDesc(texture) {}

    // Быстрая проверка на валидность
    inline bool isValid() const {
        return dataIdx != ~size_t{0};
    }
};