//
// Created by A on 18.05.2026.
//
module;

#include <map>
#include <memory>

#include "voxel_object_actions.h"

#include <glm/vec2.hpp>
#include <glm/trigonometric.hpp>
#include <glm/ext/scalar_constants.hpp>

import voxel_object;

export module voxel_module;

export
class VoxelModule
{
protected:
    float integrity = 0.f;
    std::vector<std::pair<int, int>> voxel_coords;

public:
    virtual ~VoxelModule() = default;

    const int priority = 0; // defines order of actions invoking
    const float critical_integrity;
    const glm::vec2 center;

    VoxelModule() = delete;

    VoxelModule(const int& priority, const glm::vec2& center, const float& critical_integrity, const std::vector<std::pair<int, int>>& coords)
        : priority(priority)
        , center(center)
        , critical_integrity(critical_integrity)
        , voxel_coords(coords)
    {
    }

    void refresh_integrity(const std::vector<Voxel>& ship_voxels, const size_t& width)
    {
        integrity = 0;
        float deltaIntegrity = 1 / static_cast<float>(voxel_coords.size());

        for (auto& coord : voxel_coords) {
            if (ship_voxels[coord.first + coord.second * width].is_alive()) {
                integrity += deltaIntegrity;
            }
        }
    }

    virtual void apply_actions(actions& actions) = 0;
};

export
class VoxelModuleUniversalEngine : public VoxelModule
{
private:
    const float power_;
    const float torque_;

public:
    VoxelModuleUniversalEngine(
        const int& priority,
        const glm::vec2& center,
        const float& critical_integrity,
        const std::vector<std::pair<int, int>>& coords,
        const float& power,
        const float& torque)
        : VoxelModule(priority, center, critical_integrity, coords)
        , power_(power)
        , torque_(torque)
    {
    }

    void apply_actions(actions& actions) override
    {
        actions.backward.emplace_back([this](const glm::vec2& mass_center)
        {
            return power_;
        });
        actions.forward.emplace_back([this](const glm::vec2& mass_center)
        {
            return power_;
        });
        actions.left.emplace_back([this](const glm::vec2& mass_center)
        {
            return power_;
        });
        actions.right.emplace_back([this](const glm::vec2& mass_center)
        {
            return power_;
        });
        actions.yawLeft.emplace_back([this](const glm::vec2& mass_center)
        {
            return torque_;
        });
        actions.yawRight.emplace_back([this](const glm::vec2& mass_center)
        {
            return torque_;
        });
    }
};

export
class VoxelModuleTurret : public VoxelModule
{
private:
    VoxelObject& turret_object_;
    ProjectileMakeupData projectile_data_;
    float cooldown_;
    float timer_ = 0.f;

public:
    const float turn_speed;

    VoxelModuleTurret(
        const int& priority,
        const glm::vec2& center,
        const float& critical_integrity,
        const std::vector<std::pair<int, int>>& coords,
        VoxelObject& object,
        const float& turn_speed,
        const int& parent_transform,
        const float& cooldown,
        const ProjectileMakeupData& projectile_data)
        : VoxelModule(priority, center, critical_integrity, coords)
        , turret_object_(object)
        , turn_speed(turn_speed)
        , cooldown_(cooldown)
        , projectile_data_(projectile_data)
    {
        turret_object_.set_parent_transform(parent_transform);
    }

    void apply_actions(actions& actions) override
    {
        actions.target.emplace_back([this](const glm::vec2& target)
        {
            glm::vec2 direction = turret_object_.global_position() - target;
            float angle = std::atan2(direction.y, direction.x);
            turret_object_.set_global_angle(angle);
        });
        actions.primaryWeapon.emplace_back([this](const float& dt)
        {
            timer_ -= dt;

            if (timer_ < 0.f) {
                timer_ = cooldown_;
                ProjectileMakeupData new_projectile_data = projectile_data_;

                float angle = turret_object_.global_rotation() + glm::pi<float>();
                float cosA = std::cosf(angle);
                float sinA = std::sinf(angle);
                float& velX = new_projectile_data.velocity.x;
                float& velY = new_projectile_data.velocity.y;
                new_projectile_data.velocity = {
                    velX * cosA - velY * sinA,
                    velX * sinA + velY * cosA};

                glm::vec2 turretPosition = turret_object_.global_position();
                float& localPosX = new_projectile_data.position.x;
                float& localPosY = new_projectile_data.position.y;
                new_projectile_data.position = {
                    localPosX * cosA - localPosY * sinA + turretPosition.x,
                    localPosX * sinA + localPosY * cosA + turretPosition.y};


                return std::optional(new_projectile_data);
            }
            else {
                return std::optional<ProjectileMakeupData>();
            }
        });
    }
};