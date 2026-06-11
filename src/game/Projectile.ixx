//
// Created by A on 08.06.2026.
//
module;

#include "explosion_data.h"
#include "physic_parameters.h"
#include "graphics/uv_rect.h"

#include <memory>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

export module projectile;

import texture;
import sprite_repository;
import sprite;
import scene_object;
import recursive_hitbox;

export
class Projectile : public SceneObject
{
private:
    SpritesRepository& repo_;
    physic_parameters physic_;
    const texture& tex_;
    size_t texture_id_;
    size_t sprite_;
    explosion_data explosion_;
    float integrity_;

    RecursiveHitbox hitbox_;

public:
    Projectile(
    SpritesRepository& repository,
    const texture& tex,
    const size_t texture_id,
    const uv_rect& local_UV,
    const kinetic_parameters& kinetic,
    const glm::vec2& size,
    const explosion_data& explosion,
    const float& integrity,
    const glm::vec2& position = {},
    const glm::vec2& velocity = {})
    : repo_(repository)
    , tex_(tex)
    , texture_id_(texture_id)
    , physic_({velocity, 0.f, kinetic.mass, kinetic.moment})
    , explosion_(explosion)
    , integrity_(integrity)
    , hitbox_({0, 0, 1, 1}, 1, size, size * 0.5f)
    {
        SpriteData sd;
        sd.width      = size.x;
        sd.height     = size.y;
        sd.angle      = 0.f;
        sd.depth      = 0;
        sd.position = position;
        sd.paralax    = 1;
        sd.uvRect     = local_UV;
        sd.transformIdx = ~0u;

        sprite_ = repo_.addSprite(sd, sd.depth);
    }

    Projectile(const Projectile& other)
    : repo_(other.repo_)
    , tex_(other.tex_)
    , texture_id_(other.texture_id_)
    , physic_({other.get_velocity(), 0.f, other.get_mass(), other.get_moment()})
    , explosion_(other.explosion_)
    , integrity_(other.integrity_)
    , hitbox_({0, 0, 1, 1}, 1, other.hitbox_.get_size(), other.hitbox_.get_size() * 0.5f)
    {
        SpriteData sd;
        sd.width      = tex_.width;
        sd.height     = tex_.height;
        sd.angle      = 0.f;
        sd.depth      = 0;
        glm::vec2 pos = other.get_position();
        sd.position = pos;
        sd.paralax    = 1;
        sd.uvRect     = other.getUV();
        sd.transformIdx = ~0u;

        sprite_ = repo_.addSprite(sd, sd.depth);
    }


    const float& get_angle() const override
    {
        return repo_.getDataPointer(sprite_)->angle;
    }
    void add_angle(const float& dAngle) override
    {
        repo_.getDataPointer(sprite_)->angle += dAngle;
    }
    void set_angle(const float& angle) override
    {
        repo_.getDataPointer(sprite_)->angle = angle;
    }

    void add_position(const glm::vec2& pos) override
    {
        repo_.getDataPointer(sprite_)->position += pos;
    }
    void set_position(const glm::vec2& pos) override
    {
        repo_.getDataPointer(sprite_)->position = pos;
    }
    const glm::vec2& get_position() const override
    {
        return repo_.getDataPointer(sprite_)->position;
    }

    const float& get_angular_velocity() const override
    {
        return physic_.speed.angularVelocity;
    }
    void add_angular_velocity(const float& dVelocity) override
    {
        physic_.speed.angularVelocity += dVelocity;
    }
    void set_angular_velocity(const float& velocity) override
    {
        physic_.speed.angularVelocity = velocity;
    }

    const glm::vec2& get_velocity() const override
    {
        return physic_.speed.linearVelocity;
    }
    void add_velocity(const glm::vec2& velocity) override
    {
        physic_.speed.linearVelocity += velocity;
    }
    void set_velocity(const glm::vec2& velocity) override
    {
        physic_.speed.linearVelocity = velocity;
    }

    const float& get_mass() const override
    {
        return physic_.kinetic.mass;
    }
    const float& get_moment() const override
    {
        return physic_.kinetic.moment;
    }

    const size_t& get_parent_texture() const override
    {
        return texture_id_;
    }

    const RecursiveHitbox& get_hitbox() const
    {
        return hitbox_;
    }

    std::vector<std::unique_ptr<events::SceneEvent>> on_collision() override
    {
        std::vector<std::unique_ptr<events::SceneEvent>> events;
        events.reserve(2);
        events.push_back(std::make_unique<events::SpawnExplosion>(explosion_, get_position()));
        events.push_back(std::make_unique<events::SelfDestruct>());
        return events;
    }

    void update(const float& dt) override
    {
        add_position(physic_.speed.linearVelocity * dt);
        add_angle(physic_.speed.angularVelocity * dt);
    }

    void apply_impulse_at(const glm::vec2& impulse, const glm::vec2& world_pos) override {
        // 1. Изменение линейной скорости: v += J / m
        physic_.speed.linearVelocity += impulse / physic_.kinetic.mass;

        // 2. Изменение угловой скорости: w += (r x J) / I
        glm::vec2 r = world_pos - get_position(); // плечо силы
        float torque = r.x * impulse.y - r.y * impulse.x; // 2D cross product
        physic_.speed.angularVelocity += torque / physic_.kinetic.moment;
    }

    std::optional<glm::vec2> find_contact(const PhysicObject& other) const override
    {
        return RecursiveHitbox::checkCollision(
                RecursiveHitbox::CollisionCheckerInput(hitbox_, get_position(), get_angle()),
                RecursiveHitbox::CollisionCheckerInput(other.get_hitbox(), other.get_position(), other.get_angle())
            );
    }

    void area_damage(const explosion_data& explosion, const glm::vec2& position) override
    {
        return; // TODO: check if collide
    }

    ~Projectile()
    {
        repo_.removeSprite(sprite_);
    }

private:
    const uv_rect& getUV() const
    {
        return repo_.getDataPointer(sprite_)->uvRect;
    }
};
