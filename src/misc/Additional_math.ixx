//
// Created by A on 14.06.2026.
//
module;

#include <cmath>
#include <glm/vec2.hpp>

export module additional_math;

export
namespace math
{
    inline glm::vec2 rotate_vec2(const glm::vec2& vector, const float& angle)
    {
        float cosA = std::cosf(angle);
        float sinA = std::sinf(angle);
        return {
            vector.x * cosA - vector.y * sinA,
            vector.x * sinA + vector.y * cosA};
    }
}
