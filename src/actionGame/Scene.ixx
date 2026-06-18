module;

#include <glm/glm.hpp>

#include "explosion_data.h"

#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>
#include <mutex>
#include <queue>
#include <set>

export module scene;

export import sprite_repository;
export import texture;
export import atlas;
export import projectile;
export import scene_object;

export
struct ObjectHandle
{
    uint32_t index      = ~uint32_t{0};
    uint32_t generation = 0;

    [[nodiscard]] inline bool is_valid() const { return index != ~uint32_t{0}; }
    bool operator==(const ObjectHandle&) const = default;
};

export
class Scene
{
private:
    struct EventInvokingContext
    {
        Scene* scenePtr;
        size_t objectIndex;
    };

    struct ObjectSlot
    {
        std::unique_ptr<SceneObject> obj;
        uint32_t generation = 0;
        std::set<size_t> ignoredObjects;
        bool occupied   = false;
        bool has_physics = true;
    };

    struct EventSlot
    {
        std::unique_ptr<events::SceneEvent> eventPtr;
        size_t object_index = 0;

        EventSlot(std::unique_ptr<events::SceneEvent> event, size_t parent_object_index)
        {
            eventPtr = std::move(event);
            object_index = parent_object_index;
        }
    };

    struct Signature
    {
        uint32_t  slot_idx;
        glm::vec2 position;
        float     radius;
    };

    std::vector<EventSlot> events_;
    std::vector<ObjectSlot> slots_;
    mutable std::mutex mutex_;

public:
    explicit Scene() {}

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&)                 = delete;
    Scene& operator=(Scene&&)      = delete;

    // -----------------------------------------------------------------------
    //  Добавить объект в сцену. Возвращает хэндл для последующего доступа.
    // -----------------------------------------------------------------------
    ObjectHandle add(std::unique_ptr<SceneObject> obj, bool has_physic = true)
    {
        assert(obj != nullptr);

        // свободный слот среди удалённых
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied)
            {
                slots_[i].obj = std::move(obj);
                slots_[i].occupied = true;
                slots_[i].has_physics = has_physic;

                slots_[i].obj->on_connect_init(new EventInvokingContext(this, i), &static_add_events, i);
                // generation уже инкрементирован при удалении
                return { i, slots_[i].generation };
            }
        }

        //  расширяем
        const uint32_t idx = static_cast<uint32_t>(slots_.size());

        obj->on_connect_init(new EventInvokingContext(this, idx), &static_add_events, idx);
        slots_.push_back({std::move(obj), 0, {}, true, has_physic});
        return { idx, 0 };
    }

    bool remove(ObjectHandle handle)
    {
        if (!is_alive(handle)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        free_slot(handle.index);
        return true;
    }

    [[nodiscard]] SceneObject* get(ObjectHandle handle)
    {
        if (!is_alive(handle)) return nullptr;
        return slots_[handle.index].obj.get();
    }

    [[nodiscard]] const SceneObject* get(ObjectHandle handle) const
    {
        if (!is_alive(handle)) return nullptr;
        return slots_[handle.index].obj.get();
    }

    [[nodiscard]] bool is_alive(ObjectHandle handle) const
    {
        return handle.index < slots_.size()
            && slots_[handle.index].occupied
            && slots_[handle.index].generation == handle.generation;
    }

    void lock()
    {
        mutex_.lock();
    }

    void unlock()
    {
        mutex_.unlock();
    }

    void update(const float& dt)
    {
        std::vector<uint32_t> damaged_slots;

        resolve_collisions();

        std::sort(damaged_slots.begin(), damaged_slots.end());
        damaged_slots.erase(std::unique(damaged_slots.begin(), damaged_slots.end()),
                            damaged_slots.end());

        for_each([dt, this](const ObjectHandle& handle, SceneObject& obj)
        {
#ifndef DEBUG_COLLISION
            obj.update(dt);
#else
            obj.update(dt, slots_[handle.index].has_physics);
#endif
        });

        resolve_events(events_);
    }

    void resolve_collisions()
    {
        for (size_t ia = 0, ib = 1; ia < slots_.size(); ia++)
        for (ib = ia + 1; ib < slots_.size(); ib++) {
            if (!(slots_[ia].occupied && slots_[ib].occupied))
                continue;
            if (slots_[ia].ignoredObjects.contains(ib) || slots_[ib].ignoredObjects.contains(ia))
                continue;

            SceneObject& a = *slots_[ia].obj;
            SceneObject& b = *slots_[ib].obj;

            if (!(slots_[ib].has_physics && slots_[ia].has_physics))
                continue;

            std::optional<glm::vec2> contact = a.find_contact(b);
            if (!contact.has_value()) continue;

            glm::vec2& contact_point = contact.value();

            const float restitution = 0.6f;

            const glm::vec2 posA = a.get_position();
            const glm::vec2 posB = b.get_position();

            const float mA = a.get_mass();
            const float mB = b.get_mass();
            const float IA = a.get_moment();
            const float IB = b.get_moment();

            const glm::vec2 rA = contact_point - posA;
            const glm::vec2 rB = contact_point - posB;

            const glm::vec2 vA = a.get_velocity() + glm::vec2(-a.get_angular_velocity() * rA.y, a.get_angular_velocity() * rA.x);
            const glm::vec2 vB = b.get_velocity() + glm::vec2(-b.get_angular_velocity() * rB.y, b.get_angular_velocity() * rB.x);

            const glm::vec2 vRel = vA - vB;

            if (glm::length(vRel) == 0) continue;

            glm::vec2 n = glm::normalize(vRel);

            float velAlongNormal = glm::dot(vRel, n);

            const float raCrossN = rA.x * n.y - rA.y * n.x;
            const float rbCrossN = rB.x * n.y - rB.y * n.x;

            const float invMassSum = (1.0f / mA) + (1.0f / mB);
            const float invInertiaSum = (raCrossN * raCrossN) / IA + (rbCrossN * rbCrossN) / IB;

            if (invMassSum + invInertiaSum < 1e-9f) continue;

            float j = -(1.0f + restitution) * velAlongNormal;
            j /= (invMassSum + invInertiaSum);

            const glm::vec2 impulse = n * j;

            a.apply_impulse_at(impulse, contact_point);
            b.apply_impulse_at(-impulse, contact_point);

            // позиционная коррекция
            const glm::vec2 vADirection = glm::normalize(a.get_velocity() + glm::vec2(-a.get_angular_velocity() * rA.y, a.get_angular_velocity() * rA.x));
            const glm::vec2 vBDirection = glm::normalize(b.get_velocity() + glm::vec2(-b.get_angular_velocity() * rB.y, b.get_angular_velocity() * rB.x));
            float dotA = glm::dot(vADirection, n);
            float dotB = glm::dot(vBDirection, n);

            const float correctionMag = -0.02f;
            const glm::vec2 correction = n * correctionMag;
            a.add_position(dotA * correction * (1.0f / mA) / invMassSum);
            b.add_position(dotB * -correction * (1.0f / mB) / invMassSum);
        }
    }

    void resolve_events(std::vector<EventSlot>& eventsVector)
    {
        std::vector<EventSlot> eventsCopy;
        eventsCopy.swap(eventsVector);

        for (EventSlot& eventSlot : eventsCopy) {
            switch (eventSlot.eventPtr->type()) {
                case events::emptyEvent: break;
                case events::explosionEvent:
                {
                    auto* explosionEventPtr = static_cast<events::SpawnExplosion*>(eventSlot.eventPtr.get());
                    for_each([explosionEventPtr, this](ObjectHandle handle, SceneObject& obj)
                    {
                        if (slots_[handle.index].has_physics) {
                            obj.area_damage(explosionEventPtr->explosion, explosionEventPtr->position);
                        }
                    });

                    break;
                }
                case events::selfDestructEvent:
                {
                    free_slot(eventSlot.object_index);
                    break;
                }
                case events::addObjectEvent:
                {
                    auto* addObjectEventPtr = static_cast<events::AddObject*>(eventSlot.eventPtr.get());
                    ObjectHandle handle = add(std::move(addObjectEventPtr->objectPtr));
                    slots_[handle.index].ignoredObjects =
                        std::set(addObjectEventPtr->ignoredObjects.begin(), addObjectEventPtr->ignoredObjects.end());
                    break;
                }
            }
        }
    }

    template<typename Fn>
    void for_each(Fn&& fn)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied) continue;
            fn(ObjectHandle{ i, slots_[i].generation }, *slots_[i].obj);
        }
    }

    template<typename Fn>
    void for_each(Fn&& fn) const
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied) continue;
            fn(ObjectHandle{ i, slots_[i].generation },
               std::as_const(*slots_[i].obj));
        }
    }

    size_t object_count() const
    {
        size_t n = 0;
        for (auto& s : slots_) if (s.occupied) ++n;
        return n;
    }

    void add_event(std::unique_ptr<events::SceneEvent>& event)
    {
        events_.push_back(EventSlot(std::move(event), ~static_cast<size_t>(0)));
    }

    void add_events(std::vector<std::unique_ptr<events::SceneEvent>>& events_vector)
    {
        for (std::unique_ptr<events::SceneEvent>& eventPtr : events_vector) {
            events_.push_back(EventSlot(std::move(eventPtr), ~static_cast<size_t>(0)));
        }
    }

private:
    static void static_add_events(void* contextVoided, std::vector<std::unique_ptr<events::SceneEvent>>& events_vector)
    {
        EventInvokingContext* context = static_cast<EventInvokingContext*>(contextVoided);
        for (std::unique_ptr<events::SceneEvent>& eventPtr : events_vector) {
            context->scenePtr->events_.push_back(EventSlot(std::move(eventPtr), context->objectIndex));
        }
        events_vector.clear();
    }

    void free_slot(uint32_t idx)
    {
        slots_[idx].obj.reset();
        slots_[idx].occupied = false;
        ++slots_[idx].generation; // инвалидирует все старые хэндлы на этот слот
    }
};