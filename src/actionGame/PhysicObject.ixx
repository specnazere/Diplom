//
// Created by A on 10.06.2026.
//
module;

#include <glm/glm.hpp>

#include <optional>

export module physic_object;

export import recursive_hitbox;

export
class PhysicObject
{
public:
    virtual ~PhysicObject() = default;

    virtual void apply_impulse_at(glm::vec2 impulse, glm::vec2 global_position) = 0;

    [[nodiscard]] virtual glm::vec2 get_position() const = 0;
    virtual void add_position(glm::vec2 additional_position) = 0;
    virtual void set_position(glm::vec2 position) = 0;

    [[nodiscard]] virtual float get_angle() const = 0;
    virtual void add_angle(float additional_position) = 0;
    virtual void set_angle(float position) = 0;

    [[nodiscard]] virtual glm::vec2 get_velocity() const = 0;
    virtual void add_velocity(glm::vec2 additional_velocity) = 0;
    virtual void set_velocity(glm::vec2 velocity) = 0;

    [[nodiscard]] virtual float get_angular_velocity() const = 0;
    virtual void add_angular_velocity(float additional_angular_velocity) = 0;
    virtual void set_angular_velocity(float angular_velocity) = 0;

    [[nodiscard]] virtual float get_mass() const = 0;
    [[nodiscard]] virtual float get_moment() const = 0;

#ifndef DEBUG_COLLISION
    virtual void update(float dt) = 0;
#else
    virtual void update(float dt, bool debug_draw) = 0;
#endif

    [[nodiscard]] virtual const RecursiveHitbox& get_hitbox() const = 0;

    [[nodiscard]] virtual std::optional<glm::vec2> find_contact(const PhysicObject& other) const = 0;
};
