//
// Created by A on 18.05.2026.
//
module;

#include <iostream>
#include <map>
#include <memory>

#include "voxel_object_actions.h"

#include <glm/vec2.hpp>
#include <glm/trigonometric.hpp>
#include <glm/ext/scalar_constants.hpp>


export module voxel_module;

import voxel_object;
import sprite_repository;
import transform_node;

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
    size_t turret_sprite_;
    size_t mount_transform_;
    ProjectileMakeupData projectile_data_;
    float cooldown_;
    float timer_ = 0.f;

public:
    const float turn_speed;

    VoxelModuleTurret(
        const int& priority,
        const glm::vec2& position,
        const glm::vec2& mount,
        const float& critical_integrity,
        const std::vector<std::pair<int, int>>& coords,
        const size_t& turret_sprite,
        const float& turn_speed,
        const int& parent_transform,
        const float& cooldown,
        const ProjectileMakeupData& projectile_data)
        : VoxelModule(priority, position, critical_integrity, coords)
        , turret_sprite_(turret_sprite)
        , turn_speed(turn_speed)
        , cooldown_(cooldown)
        , projectile_data_(projectile_data)
    {
        mount_transform_ = SpritesRepository::get_active().createTransform();
        SpriteData& sprite_data = *SpritesRepository::get_active().getDataPointer(turret_sprite_);
        sprite_data.transformIdx = mount_transform_;
        sprite_data.position = mount;
        SpritesRepository::get_active().getTransform(mount_transform_).parentIndex = parent_transform;
    }

    void apply_actions(actions& actions) override
    {
        actions.target.emplace_back([this](const glm::vec2& target)
        {
            glm::vec2 direction = globalPosition() - target;
            float angle = std::atan2(direction.y, direction.x);
            setGlobalRotation(angle);
        });
        actions.primaryWeapon.emplace_back([this](const float& dt)
        {
            timer_ -= dt;

            if (timer_ < 0.f) {
                timer_ = cooldown_;
                ProjectileMakeupData new_projectile_data = projectile_data_;

                float angle = globalRotation() + glm::pi<float>();
                float cosA = std::cosf(angle);
                float sinA = std::sinf(angle);
                float& velX = new_projectile_data.velocity.x;
                float& velY = new_projectile_data.velocity.y;
                new_projectile_data.velocity = {
                    velX * cosA - velY * sinA,
                    velX * sinA + velY * cosA};

                glm::vec2 turretPosition = globalPosition();
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

private:
    inline TransformNode& turretMountData()
    {
        return SpritesRepository::get_active().getTransform(mount_transform_);
    }
    inline glm::vec2 globalPosition()
    {
        return SpritesRepository::get_active().getGlobalPosition(mount_transform_);
    }
    inline float globalRotation()
    {
        return SpritesRepository::get_active().getGlobalRotation(mount_transform_);
    }
    inline void setGlobalRotation(const float& rotation)
    {
        TransformNode& mountData = turretMountData();
        mountData.angle += rotation - globalRotation();
    }
};


// export
// class VoxelModuleControlRoom : public VoxelModule
// {
//     void apply_actions(actions& actions) override;
// };