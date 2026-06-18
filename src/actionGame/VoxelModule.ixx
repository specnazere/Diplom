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

export import voxel_object;
export import sprite_repository;
export import transform_node;

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

    VoxelModule(int priority, glm::vec2 center, float critical_integrity, const std::vector<std::pair<int, int>>& coords)
        : voxel_coords(coords)
        , priority(priority)
        , critical_integrity(critical_integrity)
        , center(center)
    {}

    void refresh_integrity(const std::vector<Voxel>& ship_voxels, size_t width)
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
        int priority,
        glm::vec2 center,
        float critical_integrity,
        const std::vector<std::pair<int, int>>& coords,
        float power,
        float torque)
        : VoxelModule(priority, center, critical_integrity, coords)
        , power_(power)
        , torque_(torque)
    {
    }

    void apply_actions(actions& actions) override
    {
        actions.backward.emplace_back([this](glm::vec2 mass_center)
        {
            return power_;
        });
        actions.forward.emplace_back([this](glm::vec2 mass_center)
        {
            return power_;
        });
        actions.left.emplace_back([this](glm::vec2 mass_center)
        {
            return power_;
        });
        actions.right.emplace_back([this](glm::vec2 mass_center)
        {
            return power_;
        });
        actions.yawLeft.emplace_back([this](glm::vec2 mass_center)
        {
            return torque_;
        });
        actions.yawRight.emplace_back([this](glm::vec2 mass_center)
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
    projectile_makeup_data projectile_data_;
    float cooldown_;
    float timer_ = 0.f;

public:
    const float turn_speed;

    VoxelModuleTurret(
        int priority,
        glm::vec2 position,
        glm::vec2 mount,
        float critical_integrity,
        const std::vector<std::pair<int, int>>& coords,
        size_t turret_sprite,
        float turn_speed,
        int parent_transform,
        float cooldown,
        const projectile_makeup_data& projectile_data)
        : VoxelModule(priority, position, critical_integrity, coords)
        , turret_sprite_(turret_sprite)
        , turn_speed(turn_speed)
        , cooldown_(cooldown)
        , projectile_data_(projectile_data)
    {
        mount_transform_ = SpritesRepository::get_active().create_transform();
        SpriteData& sprite_data = *SpritesRepository::get_active().get_data_pointer(turret_sprite_);
        sprite_data.transformIdx = mount_transform_;
        sprite_data.position = mount;
        SpritesRepository::get_active().get_transform(mount_transform_).parentIndex = parent_transform;
    }

    void apply_actions(actions& actions) override
    {
        actions.target.emplace_back([this](const glm::vec2& target)
        {
            glm::vec2 direction = global_position() - target;
            float angle = std::atan2(direction.y, direction.x);
            set_global_rotation(angle);
        });
        actions.primaryWeapon.emplace_back([this](const float& dt)
        {
            timer_ -= dt;

            if (timer_ < 0.f) {
                timer_ = cooldown_;
                projectile_makeup_data new_projectile_data = projectile_data_;

                float angle = global_rotation() + glm::pi<float>();
                float cosA = std::cosf(angle);
                float sinA = std::sinf(angle);
                float& velX = new_projectile_data.velocity.x;
                float& velY = new_projectile_data.velocity.y;
                new_projectile_data.velocity = {
                    velX * cosA - velY * sinA,
                    velX * sinA + velY * cosA};

                glm::vec2 turretPosition = global_position();
                float& localPosX = new_projectile_data.position.x;
                float& localPosY = new_projectile_data.position.y;
                new_projectile_data.position = {
                    localPosX * cosA - localPosY * sinA + turretPosition.x,
                    localPosX * sinA + localPosY * cosA + turretPosition.y};


                return std::optional(new_projectile_data);
            } else {
                return std::optional<projectile_makeup_data>();
            }
        });
    }

private:
    [[nodiscard]] inline TransformNode& turret_mount_data() const
    {
        return SpritesRepository::get_active().get_transform(mount_transform_);
    }
    [[nodiscard]] inline glm::vec2 global_position() const
    {
        return SpritesRepository::get_active().get_global_position(mount_transform_);
    }
    [[nodiscard]] inline float global_rotation() const
    {
        return SpritesRepository::get_active().get_global_rotation(mount_transform_);
    }
    inline void set_global_rotation(float rotation) const
    {
        TransformNode& mountData = turret_mount_data();
        mountData.angle += rotation - global_rotation();
    }
};