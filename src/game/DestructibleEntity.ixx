//
// Created by A on 14.05.2026.
//
module;

#include <algorithm>

#include "graphics/uv_rect.h"

#include <glm/glm.hpp>

#include <vector>
#include <functional>
#include <iostream>
#include <ostream>

export module destructible_entity;

import destructible_object;
import sprite_repository;
import texture;

export
struct EntityStats
{
    float force = 0.f;
    float torque = 0.f;
};

export
using ModuleFunc = std::function<void(EntityStats& state, float local_integrity, float global_integrity)>;

export
struct DestructibleModule
{
    int priority = 0;
    ModuleFunc func = nullptr;
    // Список координат вокселей (col, row), которые относятся к этому модулю.
    // Если список пуст, модуль считается "глобальным" и реагирует только на global_integrity.
    std::vector<std::pair<int, int>> voxel_coords;

    DestructibleModule() = default;

    DestructibleModule(int prio, ModuleFunc f, std::vector<std::pair<int, int>> coords = {})
        : priority(prio), func(std::move(f)), voxel_coords(std::move(coords)) {}
};

// Обертка над объектом и его логикой
export
class DestructibleEntity
{
public:
    struct ControlState
    {
        glm::vec2 acceleration_signal = {};
        float torque_signal = 0.f;
    };

private:
    DestructibleObject object_;
    std::vector<DestructibleModule> modules_;
    size_t initial_alive_count_ = 0;

    EntityStats stats_;
    ControlState control_signals_;

public:
    DestructibleObject& get_object() { return object_; }
    const DestructibleObject& get_object() const { return object_; }

    // Конструктор принимает все необходимое для создания DestructibleObject
    DestructibleEntity(
        SpritesRepository& repo,
        const texture& tex,
        size_t texture_id,
        const uv_rect& local_uv,
        DestructibleObject::Params params = {1.f, 1.f, 0.f, 1.f, {0.f, 0.f}},
        bool chipped = false,
        unsigned char alpha_threshold = 0
    )
        : object_(repo, tex, texture_id, local_uv, params, chipped, alpha_threshold)
    {
        initial_alive_count_ = count_alive_voxels();
        recalculate_state();
    }

    void add_module(int priority, ModuleFunc func, std::vector<std::pair<int, int>> coords = {})
    {
        if (func) {
            modules_.emplace_back(priority, std::move(func), std::move(coords));
            // Сортируем по приоритету
            std::sort(modules_.begin(), modules_.end(), [](const auto& a, const auto& b) {
                return a.priority < b.priority;
            });
            recalculate_state();
        }
    }

    // --- Основной цикл ---

    // Обновление физики и состояния
    // dt - дельта времени
    void update(float dt, bool debug = false)
    {
        // 1. Применяем скорости к позиции (встроенная физика объекта)
        object_.velocity += stats_.force / object_.get_mass() * dt * control_signals_.acceleration_signal;
        object_.angular_velocity += stats_.torque / object_.get_inertia_moment() * dt * control_signals_.torque_signal;

        object_.update_position(dt);

        if (debug) {
            std::cout << control_signals_.acceleration_signal.x << " " << control_signals_.acceleration_signal.y << std::endl;
        }

        // 2. Здесь можно добавить внешние силы, если они не являются модулями,
        //    или интеграцию с глобальным физическим движком.
    }

    // Нанесение урона с автоматическим обновлением модулей
    bool apply_damage(const glm::vec2& world_pos, float damage, float radius = 0.f)
    {
        bool changed = object_.apply_damage(world_pos, damage, radius);

        if (changed)
        {
            // Если структура изменилась, пересчитываем влияние модулей
            recalculate_state();

            // Опционально: проверка на раскол объекта
            // auto fragments = object_.split_if_disconnected();
            // if (!fragments.empty()) { ... обработка фрагментов ... }
        }

        return changed;
    }

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
        for (const auto& mod : modules_)
        {
            if (!mod.func) continue;

            float local_integrity = 1.0f;

            // Если у модуля есть конкретные воксели, считаем их целостность
            if (!mod.voxel_coords.empty())
            {
                int alive_in_module = 0;
                int total_in_module = static_cast<int>(mod.voxel_coords.size());

                for (const auto& [col, row] : mod.voxel_coords)
                {
                    const Voxel* v = object_.voxel_at(col, row);
                    if (v && v->is_alive())
                    {
                        ++alive_in_module;
                    }
                }

                local_integrity = (total_in_module > 0)
                    ? static_cast<float>(alive_in_module) / static_cast<float>(total_in_module)
                    : 0.f;
            }

            // Вызываем логику модуля
            mod.func(stats_, local_integrity, global_integrity);
        }
    }

    size_t count_alive_voxels() const
    {
        const auto& voxels = object_.voxels();
        size_t count = 0;
        for (const auto& v : voxels)
        {
            if (v.is_alive()) ++count;
        }
        return count;
    }
};