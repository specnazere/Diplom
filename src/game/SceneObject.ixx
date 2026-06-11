//
// Created by A on 11.06.2026.
//
module;

#include <glm/vec2.hpp>

#include "explosion_data.h"

#include <memory>
#include <vector>

export module scene_object;

import physic_object;
import destructible_object;

export
namespace events
{
    enum eventType
    {
        emptyEvent = 0,
        explosionEvent,
        selfDestructEvent,
        addObjectEvent,
    };

    struct SceneEvent
    {
        virtual ~SceneEvent() {}
        virtual eventType type() {return emptyEvent;}
    };
}

export
class SceneObject : public PhysicObject, public DestructibleObject
{
public:
    virtual std::vector<std::unique_ptr<events::SceneEvent>> on_collision() = 0;
    virtual const size_t& get_parent_texture() const = 0;
};

export
namespace events
{
    struct SpawnExplosion final : SceneEvent
    {
        explosion_data explosion;
        glm::vec2 position;

        SpawnExplosion(const explosion_data& explosion_data_input, const glm::vec2& position_input)
            : explosion(explosion_data_input)
            , position(position_input)
        {}

        eventType type() override {
            return explosionEvent;
        }
    };

    struct SelfDestruct final : SceneEvent
    {
        eventType type() override {
            return selfDestructEvent;
        }
    };

    struct AddObject final : SceneEvent
    {
        std::unique_ptr<SceneObject> objectPtr;

        eventType type() override {
            return addObjectEvent;
        }
    };

    // struct SpawnVoxelObject : SceneEvent
    // {
    //     size_t texture_id;
    //     voxel_object_params params;
    //     eventType type() override {
    //         return selfDestructEvent;
    //     }
    // };
    //
    // struct SpawnProjectile : SceneEvent
    // {
    //     size_t texture_id;
    //     kinetic_parameters kinetic;
    //     glm::vec2 size;
    //     explosion_data explosion;
    //     glm::vec2 position;
    //     glm::vec2 velocity;
    //
    //     eventType type() override {
    //         return spawnProjectileEvent;
    //     }
    // };
}