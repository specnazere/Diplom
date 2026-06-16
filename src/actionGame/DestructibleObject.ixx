//
// Created by A on 11.06.2026.
//
module;

#include <glm/vec2.hpp>

#include "explosion_data.h"

export module destructible_object;

export
class DestructibleObject
{
public:
    virtual ~DestructibleObject() = default;

    virtual void area_damage(const explosion_data&, const glm::vec2& position) = 0;
};
