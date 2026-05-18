//
// Created by A on 18.05.2026.
//
module;

#include <iostream>

#include "voxel_object_actions.h"

#include <glm/vec2.hpp>

#include <map>
#include <glm/trigonometric.hpp>
#include <glm/ext/quaternion_geometric.hpp>

export module voxel_module;

import destructible_object;

export
class VoxelModule
{
protected:
    float integrity = 0.f;
    std::vector<std::pair<int, int>> voxel_coords;

public:
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
    DestructibleObject& turret_object_;
public:
    const float turn_speed;

    VoxelModuleTurret(
        const int& priority,
        const glm::vec2& center,
        const float& critical_integrity,
        const std::vector<std::pair<int, int>>& coords,
        DestructibleObject& object,
        const float& turn_speed,
        const int parent_transform)
        : VoxelModule(priority, center, critical_integrity, coords)
        , turret_object_(object)
        , turn_speed(turn_speed)
    {
        turret_object_.set_parent_transform(parent_transform);
    }

    void apply_actions(actions& actions) override
    {
        actions.target.emplace_back([this](const glm::vec2& target)
        {
            glm::vec2 direction = turret_object_.global_position() - target;
            float angle = std::atan2(direction.y, direction.x);
            turret_object_.set_global_rotation(angle);
        });
    }
};