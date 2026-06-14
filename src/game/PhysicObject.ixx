//
// Created by A on 10.06.2026.
//
module;

#include <glm/glm.hpp>

#include <optional>

export module physic_object;

import recursive_hitbox;

export
class PhysicObject
{
public:
    virtual ~PhysicObject() = default;

    virtual void apply_impulse_at(const glm::vec2& impulse, const glm::vec2& global_position) = 0;

    virtual const glm::vec2& get_position() const = 0;
    virtual void add_position(const glm::vec2& additional_position) = 0;
    virtual void set_position(const glm::vec2& position) = 0;

    virtual const float& get_angle() const = 0;
    virtual void add_angle(const float& additional_position) = 0;
    virtual void set_angle(const float& position) = 0;

    virtual const glm::vec2& get_velocity() const = 0;
    virtual void add_velocity(const glm::vec2& additional_velocity) = 0;
    virtual void set_velocity(const glm::vec2& velocity) = 0;

    virtual const float& get_angular_velocity() const = 0;
    virtual void add_angular_velocity(const float& additional_angular_velocity) = 0;
    virtual void set_angular_velocity(const float& angular_velocity) = 0;

    virtual const float& get_mass() const = 0;
    virtual const float& get_moment() const = 0;

#ifndef DEBUG_COLLISION
    virtual void update(const float& dt) = 0;
#else
    virtual void update(const float& dt, bool debug_draw) = 0;
#endif

    virtual const RecursiveHitbox& get_hitbox() const = 0;

    virtual std::optional<glm::vec2> find_contact(const PhysicObject& other) const = 0;
};
