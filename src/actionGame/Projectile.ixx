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
struct ProjectileMakeupData
{
    size_t texture_id;
    uv_rect local_UV;
    kinetic_parameters kinetic;
    glm::vec2 size;
    explosion_data explosion;
    float integrity;
    glm::vec2 position = {};
    glm::vec2 velocity = {};
};

export
class Projectile : public SceneObject
{
private:
    SpritesRepository& repo_;
    physic_parameters physic_;
    size_t texture_id_;
    size_t sprite_;
    explosion_data explosion_;
    float integrity_;

    EventInvokingFunction* newEventFunc_ = nullptr;
    void* invokeFunctionContext_ = nullptr;

    RecursiveHitbox hitbox_;
    size_t onSceneId_;

public:
    Projectile(
    SpritesRepository& repository,
    ProjectileMakeupData data)
    : repo_(repository)
    , physic_({data.velocity, 0.f, data.kinetic.mass, data.kinetic.moment})
    , texture_id_(data.texture_id)
    , explosion_(data.explosion)
    , integrity_(data.integrity)
    , hitbox_({0, 0, 1, 1}, 1, data.size * 0.5f, data.size * 0.25f)
{
        SpriteData sd;
        sd.width      = data.size.x;
        sd.height     = data.size.y;
        sd.angle      = std::atan2(data.velocity.y, data.velocity.x);
        sd.depth      = 0;
        sd.position = data.position;
        sd.paralax    = 1;
        sd.uvRect     = data.local_UV;
        sd.transformIdx = ~0u;
        sd.color = {255, 255, 255, 255};

        sprite_ = repo_.addSprite(sd);
        Projectile::set_velocity(data.velocity);
        Projectile::set_position(data.position);
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

    const size_t& id() override
    {
        return onSceneId_;
    }

    void onConnectInit(void* context, EventInvokingFunction* func, const size_t& id) override
    {
        invokeFunctionContext_ = context;
        newEventFunc_ = func;
        onSceneId_ = id;
    }

    void invokeEventsFromThis(std::vector<std::unique_ptr<events::SceneEvent>> outside_events) override
    {
        newEventFunc_(invokeFunctionContext_, outside_events);
    }


#ifndef DEBUG_COLLISION
    void update(const float& dt) override
#else
    void update(const float& dt, bool debug_draw) override
#endif
    {
        add_position(physic_.speed.linearVelocity * dt);
        add_angle(physic_.speed.angularVelocity * dt);

#ifdef DEBUG_COLLISION
        if (debug_draw) {
            DebugDraw();
        }
#endif
    }

#ifdef DEBUG_COLLISION
    void DebugDraw() const override
    {
        hitbox_.debugDraw(get_position(), get_angle());
    }
#endif

    void apply_impulse_at(const glm::vec2& impulse, const glm::vec2& world_pos) override {
        // 1. Изменение линейной скорости: v += J / m
        physic_.speed.linearVelocity += impulse / physic_.kinetic.mass;

        // 2. Изменение угловой скорости: w += (r x J) / I
        glm::vec2 r = world_pos - get_position(); // плечо силы
        float torque = r.x * impulse.y - r.y * impulse.x; // 2D cross product
        physic_.speed.angularVelocity += torque / physic_.kinetic.moment;

        invoke_explosion();
    }

    void invoke_explosion()
    {
        std::vector<std::unique_ptr<events::SceneEvent>> events;
        events.reserve(2);
        events.push_back(std::make_unique<events::SpawnExplosion>(explosion_, get_position()));
        events.push_back(std::make_unique<events::SelfDestruct>());
        newEventFunc_(invokeFunctionContext_, events);
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
