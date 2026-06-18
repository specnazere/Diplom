//
// Created by A on 11.06.2026.
//
module;

#include <glm/vec2.hpp>

#include "explosion_data.h"

#include <memory>
#include <vector>

#ifdef DEBUG_COLLISION
#include "imgui/imgui_impl_opengl3_loader.h"
#endif

export module scene_object;

export import physic_object;
export import destructible_object;

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
        virtual ~SceneEvent() = default;
        virtual eventType type() {return emptyEvent;}
    };
}

export
class SceneObject : public PhysicObject, public DestructibleObject
{
public:
    using EventInvokingFunction = void(void* context, std::vector<std::unique_ptr<events::SceneEvent>>& new_events);

    virtual void on_connect_init(void* context, EventInvokingFunction* func, size_t id) = 0;
    [[nodiscard]] virtual size_t id() const = 0;
    virtual void invoke_events_from_this(std::vector<std::unique_ptr<events::SceneEvent>>) = 0;
    [[nodiscard]] virtual size_t get_parent_texture() const = 0;

#ifdef DEBUG_COLLISION
    virtual void DebugDraw() const = 0;
#endif
};

export
namespace events
{
    struct SpawnExplosion final : SceneEvent
    {
        explosion_data explosion;
        glm::vec2 position;

        SpawnExplosion(explosion_data explosion_data_input, glm::vec2 position_input)
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
        std::vector<size_t> ignoredObjects;

        explicit AddObject(std::unique_ptr<SceneObject> newObjectPtr, const std::vector<size_t>& ignoredObjects_input = {})
        {
            objectPtr = std::move(newObjectPtr);
            ignoredObjects = ignoredObjects_input;
        }

        eventType type() override {
            return addObjectEvent;
        }
    };
}