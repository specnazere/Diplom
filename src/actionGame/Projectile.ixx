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

export import texture;
export import sprite_repository;
export import sprite;
export import scene_object;
export import recursive_hitbox;

export
struct projectile_makeup_data
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
    physic_parameters physic_;
    size_t texture_id_;
    size_t sprite_;
    explosion_data explosion_;
    float integrity_;

    EventInvokingFunction* newEventFunc_ = nullptr;
    void* invokeFunctionContext_ = nullptr;

    RecursiveHitbox hitbox_;
    size_t onSceneId_{};

public:
    Projectile(
    projectile_makeup_data data)
    : physic_({data.velocity, 0.f, data.kinetic.mass, data.kinetic.moment})
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

        auto& repo = SpritesRepository::get_active();
        sprite_ = repo.add_sprite(sd);
        Projectile::set_velocity(data.velocity);
        Projectile::set_position(data.position);
    }

    [[nodiscard]] float get_angle() const override
    {
        return SpritesRepository::get_active().get_data_pointer(sprite_)->angle;
    }
    void add_angle(float dAngle) override
    {
        SpritesRepository::get_active().get_data_pointer(sprite_)->angle += dAngle;
    }
    void set_angle(float angle) override
    {
        SpritesRepository::get_active().get_data_pointer(sprite_)->angle = angle;
    }

    [[nodiscard]] glm::vec2 get_position() const override
    {
        return SpritesRepository::get_active().get_data_pointer(sprite_)->position;
    }
    void add_position(glm::vec2 pos) override
    {
        SpritesRepository::get_active().get_data_pointer(sprite_)->position += pos;
    }
    void set_position(glm::vec2 pos) override
    {
        SpritesRepository::get_active().get_data_pointer(sprite_)->position = pos;
    }

    [[nodiscard]] float get_angular_velocity() const override
    {
        return physic_.speed.angularVelocity;
    }
    void add_angular_velocity(float dVelocity) override
    {
        physic_.speed.angularVelocity += dVelocity;
    }
    void set_angular_velocity(float velocity) override
    {
        physic_.speed.angularVelocity = velocity;
    }

    [[nodiscard]] glm::vec2 get_velocity() const override
    {
        return physic_.speed.linearVelocity;
    }
    void add_velocity(glm::vec2 velocity) override
    {
        physic_.speed.linearVelocity += velocity;
    }
    void set_velocity(glm::vec2 velocity) override
    {
        physic_.speed.linearVelocity = velocity;
    }

    [[nodiscard]] float get_mass() const override
    {
        return physic_.kinetic.mass;
    }
    [[nodiscard]] float get_moment() const override
    {
        return physic_.kinetic.moment;
    }

    [[nodiscard]] size_t get_parent_texture() const override
    {
        return texture_id_;
    }

    [[nodiscard]] const RecursiveHitbox& get_hitbox() const
    {
        return hitbox_;
    }

    [[nodiscard]] size_t id() const override
    {
        return onSceneId_;
    }

    void on_connect_init(void* context, EventInvokingFunction* func, size_t id) override
    {
        invokeFunctionContext_ = context;
        newEventFunc_ = func;
        onSceneId_ = id;
    }

    void invoke_events_from_this(std::vector<std::unique_ptr<events::SceneEvent>> outside_events) override
    {
        newEventFunc_(invokeFunctionContext_, outside_events);
    }


#ifndef DEBUG_COLLISION
    void update(float dt) override
#else
    void update(float dt, bool debug_draw) override
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

    void apply_impulse_at(glm::vec2 impulse, glm::vec2 world_pos) override {
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

    [[nodiscard]] std::optional<glm::vec2> find_contact(const PhysicObject& other) const override
    {
        return RecursiveHitbox::checkCollision(
                RecursiveHitbox::CollisionCheckerInput(hitbox_, get_position(), get_angle()),
                RecursiveHitbox::CollisionCheckerInput(other.get_hitbox(), other.get_position(), other.get_angle())
            );
    }

    void area_damage(explosion_data explosion, glm::vec2 position) override
    {
        // TODO: check if collide
    }

    ~Projectile() override
    {
        SpritesRepository::get_active().free_sprite(sprite_);
    }

private:
    [[nodiscard]] uv_rect get_uv() const
    {
        return SpritesRepository::get_active().get_data_pointer(sprite_)->uvRect;
    }
};
