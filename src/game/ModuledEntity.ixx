//
// Created by A on 14.05.2026.
//
module;

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include <functional>
#include <memory>
#include <ostream>

#include "graphics/uv_rect.h"
#include "voxel_object_actions.h"
#include "voxel_object_params.h"

export module destructible_entity;

import voxel_object;
import sprite_repository;
import texture;
import voxel_module;
import scene_object;

export
struct EntityStats
{
    glm::vec2 force {};
    float torque = 0.f;
};

export
namespace signals
{
    using signal_t = uint32_t;
    constexpr signal_t forwardSignal = 1u << forward;
    constexpr signal_t backwardSignal = 1u << backward;
    constexpr signal_t leftSignal = 1u << left;
    constexpr signal_t rightSignal = 1u << right;
    constexpr signal_t yawLeftSignal = 1u << yawLeft;
    constexpr signal_t yawRightSignal = 1u << yawRight;
    constexpr signal_t primaryWeaponSignal = 1u << primaryWeapon;
}

// Обертка над объектом и его логикой
export
class ModuledEntity
{
public:
    struct ControlState
    {
        glm::vec2 acceleration_signal = {};
        float torque_signal = 0.f;
    };

private:
    SpritesRepository& repo_;
    std::unique_ptr<VoxelObject> object_;
    std::vector<std::unique_ptr<VoxelModule>> modules_;
    size_t initial_alive_count_ = 0;
    actions actions_;

    EntityStats stats_;
    ControlState control_signals_;

public:
    VoxelObject& get_object() { return *object_.get(); }
    const VoxelObject& get_object() const { return *object_.get(); }
    signals::signal_t signal;
    glm::vec2 target;

    // Конструктор принимает все необходимое для создания DestructibleObject
    ModuledEntity(
        SpritesRepository& repo,
        const texture& tex,
        size_t texture_id,
        const uv_rect& local_uv,
        voxel_object_params params = VoxelObject::standart_params,
        bool chipped = false,
        unsigned char alpha_threshold = 0)
        : repo_(repo)
    {
        object_ = std::make_unique<VoxelObject>(repo, tex, texture_id, local_uv, params, chipped, alpha_threshold);
        initial_alive_count_ = count_alive_voxels();
        recalculate_state();
    }

    void add_module(std::unique_ptr<VoxelModule> module)
    {
        modules_.push_back(std::move(module));
        modules_.back()->apply_actions(actions_);
        // Сортируем по приоритету
        // std::sort(modules_.begin(), modules_.end(), [](const auto& a, const auto& b) {
        //     return a.priority < b.priority;
        // });
        // recalculate_state();
        // if (object) {
        //     modules_.back().voxel_object->set_parent_transform(object_.get_local_transform());
        // }
    }

    // --- Основной цикл ---

    // Обновление физики и состояния
    // dt - дельта времени
    void update(const float& dt)
    {
        stats_ = {};

        if (signal & signals::forwardSignal) {
            for (auto& func : actions_.forward) {
                stats_.force.x -= func({0.f, 0.f});
            }
        }
        if (signal & signals::backwardSignal) {
            for (auto& func : actions_.backward) {
                stats_.force.x += func({0.f, 0.f});
            }
        }
        if (signal & signals::leftSignal) {
            for (auto& func : actions_.left) {
                stats_.force.y -= func({0.f, 0.f});
            }
        }
        if (signal & signals::rightSignal) {
            for (auto& func : actions_.right) {
                stats_.force.y += func({0.f, 0.f});
            }
        }
        if (signal & signals::yawLeftSignal) {
            for (auto& func : actions_.yawLeft) {
                stats_.torque -= func({0.f, 0.f});
            }
        }
        if (signal & signals::yawRightSignal) {
            for (auto& func : actions_.yawRight) {
                stats_.torque += func({0.f, 0.f});
            }
        }
        if (signal & signals::primaryWeaponSignal) {
            for (auto& fire_action : actions_.primaryWeapon) {
                std::optional<ProjectileMakeupData> projectile_data = fire_action(dt);
                if (projectile_data.has_value()) {
                    std::vector<std::unique_ptr<events::SceneEvent>> events;
                    events.push_back(std::make_unique<events::AddObject>(std::make_unique<Projectile>(repo_, projectile_data.value())));
                    object_->invokeEventsFromThis(std::move(events));
                }
            }
        }

        for (auto& func : actions_.target) {
            func(target);
        }

        // 1. Применяем скорости к позиции (встроенная физика объекта)
        glm::vec2 unrotated_acceleration = stats_.force / object_->get_mass() * dt;
        float angle = object_->local_rotation();
        float cosA = std::cosf(angle);
        float sinA = std::sinf(angle);
        object_->add_velocity({
            unrotated_acceleration.x * cosA - unrotated_acceleration.y * sinA,
            unrotated_acceleration.y * cosA + unrotated_acceleration.x * sinA});
        object_->add_angular_velocity(stats_.torque / object_->get_moment() * dt);

        //object_->update(dt);
    }

    // Нанесение урона с автоматическим обновлением модулей
    // bool apply_damage(const glm::vec2& world_pos, float damage, float radius = 0.f)
    // {
    //     bool changed = object_.apply_damage(world_pos, damage, radius);
    //
    //     if (changed)
    //     {
    //         // Если структура изменилась, пересчитываем влияние модулей
    //         recalculate_state();
    //
    //         // Опционально: проверка на раскол объекта
    //         // auto fragments = object_.split_if_disconnected();
    //         // if (!fragments.empty()) { ... обработка фрагментов ... }
    //     }
    //
    //     return changed;
    // }

    void apply_acceleration_signal(const glm::vec2& acceleration_signal)
    {
        control_signals_.acceleration_signal = acceleration_signal;
    }

    void apply_torque_signal(const float& torque_signal)
    {
        control_signals_.torque_signal = torque_signal;
    }

private:
    void recalculate_state()
    {
        // 1. Считаем общую целостность
        size_t current_alive_total = count_alive_voxels();
        float global_integrity = (initial_alive_count_ > 0)
            ? static_cast<float>(current_alive_total) / static_cast<float>(initial_alive_count_)
            : 0.f;

        // 2. Очищаем состояние
        stats_ = {};

        // 3. Применяем модули
        // for (const auto& mod : modules_)
        // {
        //     //if (!mod.func) continue;
        //
        //     float local_integrity = 1.0f;
        //
        //     // Если у модуля есть конкретные воксели, считаем их целостность
        //     if (!mod.voxel_coords.empty())
        //     {
        //         int alive_in_module = 0;
        //         int total_in_module = static_cast<int>(mod.voxel_coords.size());
        //
        //         for (const auto& [col, row] : mod.voxel_coords)
        //         {
        //             const Voxel* v = object_.voxel_at(col, row);
        //             if (v && v->is_alive())
        //             {
        //                 ++alive_in_module;
        //             }
        //         }
        //
        //         local_integrity = (total_in_module > 0)
        //             ? static_cast<float>(alive_in_module) / static_cast<float>(total_in_module)
        //             : 0.f;
        //     }
        //
        //     // Вызываем логику модуля
        //     mod.func(stats_, local_integrity, global_integrity);
        // }
    }

    size_t count_alive_voxels() const
    {
        const auto& voxels = object_->voxels();
        size_t count = 0;
        for (const auto& v : voxels)
        {
            if (v.is_alive()) ++count;
        }
        return count;
    }
};