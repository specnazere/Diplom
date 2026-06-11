module;

#include "glm/glm.hpp"

#include "explosion_data.h"

#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <utility>
#include <mutex>
#include <queue>

export module scene;

import sprite_repository;
import texture;
import atlas;
import projectile;
import scene_object;

// ---------------------------------------------------------------------------
//  ObjectHandle — идентификатор объекта в сцене.
//  Поколение (generation) защищает от обращения к удалённому слоту.
// ---------------------------------------------------------------------------
export
struct ObjectHandle
{
    uint32_t index      = ~uint32_t{0};
    uint32_t generation = 0;

    bool is_valid() const { return index != ~uint32_t{0}; }
    bool operator==(const ObjectHandle&) const = default;
};

export
class Scene
{
private:
    struct ObjectSlot
    {
        std::unique_ptr<SceneObject> obj;
        uint32_t generation = 0;
        bool occupied   = false;
        bool has_physics = true;
    };

    struct EventSlot
    {
        std::unique_ptr<events::SceneEvent> eventPtr;
        size_t object_index = 0;

        EventSlot(std::unique_ptr<events::SceneEvent>& event, const size_t& parent_object_index)
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

    std::vector<EventSlot> events;

    std::vector<ObjectSlot> slots_;
    std::vector<Signature> signatures_; // живёт один кадр, пересобирается в update()

    mutable std::mutex mutex_;       // Added

public:

    explicit Scene()
    {}

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&)                 = default;
    Scene& operator=(Scene&&)      = default;

    // -----------------------------------------------------------------------
    //  Добавить объект в сцену. Возвращает хэндл для последующего доступа.
    // -----------------------------------------------------------------------
    ObjectHandle add(std::unique_ptr<SceneObject> obj, bool has_physic = true)
    {
        assert(obj != nullptr);

        std::lock_guard<std::mutex> lock(mutex_);
        // Ищем свободный слот среди удалённых
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied)
            {
                slots_[i].obj = std::move(obj);
                slots_[i].occupied = true;
                slots_[i].has_physics = has_physic;
                // generation уже инкрементирован при удалении
                return { i, slots_[i].generation };
            }
        }

        // Нет свободных слотов — расширяем
        const uint32_t idx = static_cast<uint32_t>(slots_.size());
        slots_.push_back({ std::move(obj), 0, true, has_physic});
        return { idx, 0 };
    }

    // -----------------------------------------------------------------------
    //  Удалить объект по хэндлу. Возвращает false если хэндл устарел.
    // -----------------------------------------------------------------------
    bool remove(ObjectHandle handle)
    {
        if (!is_alive(handle)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        free_slot(handle.index);
        return true;
    }

    // -----------------------------------------------------------------------
    //  Получить указатель на объект (nullptr если хэндл устарел).
    // -----------------------------------------------------------------------
    SceneObject* get(ObjectHandle handle)
    {
        if (!is_alive(handle)) return nullptr;
        return slots_[handle.index].obj.get();
    }
    const SceneObject* get(ObjectHandle handle) const
    {
        if (!is_alive(handle)) return nullptr;
        return slots_[handle.index].obj.get();
    }

    // -----------------------------------------------------------------------
    //  Проверить, жив ли хэндл
    // -----------------------------------------------------------------------
    bool is_alive(ObjectHandle handle) const
    {
        return handle.index < slots_.size()
            && slots_[handle.index].occupied
            && slots_[handle.index].generation == handle.generation;
    }

    // -----------------------------------------------------------------------
    //  Manual lock/unlock for external synchronization
    // -----------------------------------------------------------------------
    void lock()
    {
        mutex_.lock();
    }

    void unlock()
    {
        mutex_.unlock();
    }

    // -----------------------------------------------------------------------
    //  Обновить сцену за один кадр.
    //
    //  Алгоритм:
    //    1. Собрать сигнатуры всех живых объектов.
    //    2. Broad-phase по сигнатурам (описывающие окружности).
    //    3. Narrow-phase через find_contacts() для прошедших broad-phase.
    //    4. Применить коллбэк и урон.
    //    5. Вызвать split_if_disconnected() на повреждённых объектах,
    //       добавить фрагменты в сцену.
    //    6. Удалить объекты, у которых не осталось живых вокселей.
    // -----------------------------------------------------------------------
    void update(float dt)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Множество слотов, которые получили урон — им нужен split
        std::vector<uint32_t> damaged_slots;

        // Narrow-phase
        resolveCollisions();

        // for (ProjectileSlot& projSlot : projectiles_) {
        //     projSlot.proj->update(dt);
        // }


        // Убираем дубликаты в damaged_slots
        std::sort(damaged_slots.begin(), damaged_slots.end());
        damaged_slots.erase(std::unique(damaged_slots.begin(), damaged_slots.end()),
                            damaged_slots.end());

        // Split + добавление фрагментов
        std::vector<std::unique_ptr<SceneObject>> new_fragments;

        // for (uint32_t idx : damaged_slots)
        // {
        //     if (!slots_[idx].occupied) continue;
        //
        //     auto frags = slots_[idx].obj->split_if_disconnected();
        //     for (auto& frag : frags)
        //         new_fragments.push_back(
        //             std::make_unique<SceneObject>(std::move(frag)));
        // }

        // Добавить фрагменты (после remove, чтобы не трогать их сразу же)
        for (auto& frag : new_fragments)
            add(std::move(frag));

        for_each([dt](const ObjectHandle& handle, SceneObject& obj)
        {
            obj.update(dt);
        });

        resolveEvents(events);
    }

    void resolveCollisions()
    {
        for (size_t ia = 0, ib = 1; ia < slots_.size(); ia++)
        for (ib = ia + 1; ib < slots_.size(); ib++) {
            if (!(slots_[ia].occupied && slots_[ib].occupied)) continue;

            SceneObject& a = *slots_[ia].obj;
            SceneObject& b = *slots_[ib].obj;

            if (!(slots_[ib].has_physics && slots_[ia].has_physics))
                continue;

            std::optional<glm::vec2> contact = a.find_contact(b);
            if (!contact.has_value()) continue;

            glm::vec2& contact_point = contact.value();

            const float restitution = 0.6f; // Попробуй разные значения: 0.0, 0.5, 0.9
            const float percent = -0.2f;     // Процент коррекции позиции (0.8 = 80% проникновения убираем за кадр)
            const float slop = 0.01f;       // Допуск проникновения (меньше этого значения не корректируем, чтобы избежать дрожания)

            const glm::vec2 posA = a.get_position();
            const glm::vec2 posB = b.get_position();

            const float mA = a.get_mass();
            const float mB = b.get_mass();
            const float IA = a.get_moment();
            const float IB = b.get_moment();

            // Векторы от центров масс до точки контакта
            const glm::vec2 rA = contact_point - posA;
            const glm::vec2 rB = contact_point - posB;

            // 2. Расчет скоростей в точке контакта
            const glm::vec2 vA = a.get_velocity() + glm::vec2(-a.get_angular_velocity() * rA.y, a.get_angular_velocity() * rA.x);
            const glm::vec2 vB = b.get_velocity() + glm::vec2(-b.get_angular_velocity() * rB.y, b.get_angular_velocity() * rB.x);

            const glm::vec2 vRel = vA - vB;

            if (glm::length(vRel) == 0) continue;

            glm::vec2 n = glm::normalize(vRel);

            // Проекция относительной скорости на нормаль
            float velAlongNormal = glm::dot(vRel, n);

            const float raCrossN = rA.x * n.y - rA.y * n.x;
            const float rbCrossN = rB.x * n.y - rB.y * n.x;

            const float invMassSum = (1.0f / mA) + (1.0f / mB);
            const float invInertiaSum = (raCrossN * raCrossN) / IA + (rbCrossN * rbCrossN) / IB;

            if (invMassSum + invInertiaSum < 1e-9f) continue;

            float j = -(1.0f + restitution) * velAlongNormal;
            j /= (invMassSum + invInertiaSum);

            const glm::vec2 impulse = n * j;

            // Применяем к обоим. Важно: импульс для B равен -impulse
            a.apply_impulse_at(impulse, contact_point);
            b.apply_impulse_at(-impulse, contact_point);

            // Позиционная коррекция (чтобы не залипали)

            if (restitution > 0.1f) {
                const float correctionMag = 0.5f; // Подбери экспериментально (в единицах твоего мира)
                const glm::vec2 correction = n * correctionMag * percent;
                a.add_position(correction * (1.0f / mA) / invMassSum); // Распределение по массам
                b.add_position(-correction * (1.0f / mB) / invMassSum);
            }

            auto aOnCollisionevents = a.on_collision();
            for (auto& eventPtr : aOnCollisionevents) {
                events.emplace_back(eventPtr, ia);
            }
            auto bOnCollisionevents = b.on_collision();
            for (auto& eventPtr : bOnCollisionevents) {
                events.emplace_back(eventPtr, ib);
            }
        }
    }

    void resolveEvents(std::vector<EventSlot>& eventsVector)
    {
        for (EventSlot& eventSlot : eventsVector ) {
            switch (eventSlot.eventPtr->type()) {
                case events::emptyEvent: break;
                case events::explosionEvent:
                {
                    auto* explosionEventPtr = static_cast<events::SpawnExplosion*>(eventSlot.eventPtr.get());
                    for_each([explosionEventPtr](ObjectHandle handle, SceneObject& obj)
                    {
                        obj.area_damage(explosionEventPtr->explosion, explosionEventPtr->position);
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
                    add(std::move(addObjectEventPtr->objectPtr));
                }
                // case events::spawnVoxelObjectEvent:
                // {
                //     auto* spawnVoxelObjectEventPtr = static_cast<events::SpawnVoxelObject*>(eventSlot.eventPtr.get());
                //
                //     add(std::make_unique<VoxelObject>(
                //         sprites_repo_,
                //
                //     ))
                //
                //     break;
                // }
                // case events::spawnProjectileEvent:
                // {
                //     auto* spawnProjectileEventPtr = static_cast<events::SpawnProjectile*>(eventSlot.eventPtr.get());
                //     add(std::make_unique<Projectile>(
                //         repo_,
                //         ));
                //
                //     break;
                // }
            }
        }
        eventsVector.clear();
    }

    // -----------------------------------------------------------------------
    //  Итерация по всем живым объектам
    // -----------------------------------------------------------------------

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
        events.push_back(EventSlot{event, ~static_cast<size_t>(0)});
    }

private:
    // -----------------------------------------------------------------------
    //  Освободить слот, инкрементировав поколение
    // -----------------------------------------------------------------------
    void free_slot(uint32_t idx)
    {
        slots_[idx].obj.reset();
        slots_[idx].occupied = false;
        ++slots_[idx].generation; // инвалидирует все старые хэндлы на этот слот
    }
};