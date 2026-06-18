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

export module moduled_entity;

export import voxel_object;
export import sprite_repository;
export import texture;
export import voxel_module;
export import scene_object;

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
    constexpr signal_t FORWARD_SIGNAL = 1u << forward;
    constexpr signal_t BACKWARD_SIGNAL = 1u << backward;
    constexpr signal_t LEFT_SIGNAL = 1u << left;
    constexpr signal_t RIGHT_SIGNAL = 1u << right;
    constexpr signal_t YAW_LEFT_SIGNAL = 1u << yawLeft;
    constexpr signal_t YAW_RIGHT_SIGNAL = 1u << yawRight;
    constexpr signal_t PRIMAPY_WEAPON_SIGNAL = 1u << primaryWeapon;
}

// Обертка над объектом и его логикой
export
template<typename Object_t = VoxelObject>
requires std::is_base_of_v<VoxelObject, Object_t>
class ModuledEntity
{
public:
    struct ControlState
    {
        glm::vec2 acceleration_signal = {};
        float torque_signal = 0.f;
    };

private:
    std::unique_ptr<Object_t> object_;
    std::vector<std::unique_ptr<VoxelModule>> modules_;
    size_t initial_alive_count_ = 0;
    actions actions_;

    EntityStats stats_;
    ControlState control_signals_;

public:
    Object_t& get_object() { return *object_; }
    const Object_t& get_object() const { return *object_; }
    signals::signal_t signal = 0;
    glm::vec2 target = {0, 0};

    // Конструктор принимает все необходимое для создания DestructibleObject
    ModuledEntity(
        const texture& tex,
        size_t texture_id,
        uv_rect local_uv,
        voxel_object_params params = Object_t::standart_params,
        bool chipped = false,
        unsigned char alpha_threshold = 0)
    {
        object_ = std::make_unique<Object_t>(tex, texture_id, local_uv, params, chipped, alpha_threshold);
        initial_alive_count_ = count_alive_voxels();
        recalculate_state();
    }

    ModuledEntity(
        std::unique_ptr<Object_t> object)
        : object_(std::move(object))
    {
        initial_alive_count_ = count_alive_voxels();
        recalculate_state();
    }

    void add_module(std::unique_ptr<VoxelModule> module)
    {
        modules_.push_back(std::move(module));
        modules_.back()->apply_actions(actions_);
        // TODO: сортировка по приоритету
    }

    // основной цикл
    void update(float dt)
    {
        stats_ = {};

        if (signal & signals::FORWARD_SIGNAL) {
            for (auto& func : actions_.forward) {
                stats_.force.x -= func({0.f, 0.f});
            }
        }
        if (signal & signals::BACKWARD_SIGNAL) {
            for (auto& func : actions_.backward) {
                stats_.force.x += func({0.f, 0.f});
            }
        }
        if (signal & signals::LEFT_SIGNAL) {
            for (auto& func : actions_.left) {
                stats_.force.y -= func({0.f, 0.f});
            }
        }
        if (signal & signals::RIGHT_SIGNAL) {
            for (auto& func : actions_.right) {
                stats_.force.y += func({0.f, 0.f});
            }
        }
        if (signal & signals::YAW_LEFT_SIGNAL) {
            for (auto& func : actions_.yawLeft) {
                stats_.torque -= func({0.f, 0.f});
            }
        }
        if (signal & signals::YAW_RIGHT_SIGNAL) {
            for (auto& func : actions_.yawRight) {
                stats_.torque += func({0.f, 0.f});
            }
        }
        if (signal & signals::PRIMAPY_WEAPON_SIGNAL) {
            for (auto& fire_action : actions_.primaryWeapon) {

                std::optional<projectile_makeup_data> projectile_data = fire_action(dt);
                if (projectile_data.has_value()) {
                    std::vector<std::unique_ptr<events::SceneEvent>> events;
                    projectile_makeup_data projectile = projectile_data.value();
                    projectile.velocity += object_->get_velocity();
                    events.push_back(std::make_unique<events::AddObject>(std::make_unique<Projectile>(projectile), std::vector{object_->id()}));
                    object_->invoke_events_from_this(std::move(events));
                }
            }
        }

        for (auto& func : actions_.target) {
            func(target);
        }

        glm::vec2 unrotated_acceleration = stats_.force / object_->get_mass() * dt;
        float angle = object_->local_rotation();
        float cosA = std::cosf(angle);
        float sinA = std::sinf(angle);

        object_->add_velocity({
            unrotated_acceleration.x * cosA - unrotated_acceleration.y * sinA,
            unrotated_acceleration.y * cosA + unrotated_acceleration.x * sinA});
        object_->add_angular_velocity(stats_.torque / object_->get_moment() * dt);
    }

    void apply_acceleration_signal(glm::vec2 acceleration_signal)
    {
        control_signals_.acceleration_signal = acceleration_signal;
    }

    void apply_torque_signal(float torque_signal)
    {
        control_signals_.torque_signal = torque_signal;
    }

private:
    void recalculate_state()
    {
        size_t current_alive_total = count_alive_voxels();
        float global_integrity = (initial_alive_count_ > 0)
            ? static_cast<float>(current_alive_total) / static_cast<float>(initial_alive_count_)
            : 0.f;

        stats_ = {};
    }

    size_t count_alive_voxels() const
    {
        const auto& voxels = object_->voxels();
        size_t count = 0;
        for (const auto& v : voxels) {
            if (v.is_alive()) ++count;
        }
        return count;
    }
};