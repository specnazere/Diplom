//
// Created by A on 18.05.2026.
//

#ifndef VOXEL_OBJECT_ACTIONS_H
#define VOXEL_OBJECT_ACTIONS_H

#include <functional>
#include <glm/vec2.hpp>

enum action
{
    forward,
    backward,
    left,
    right,
    yawLeft,
    yawRight,
    primaryWeapon,
    targetAim,
};

// returns push
using forwardFunction = std::function<float(const glm::vec2& mass_center)>;
using backwardFunction = std::function<float(const glm::vec2& mass_center)>;
using leftFunction = std::function<float(const glm::vec2& mass_center)>;
using rightFunction = std::function<float(const glm::vec2& mass_center)>;

// returns torque
using yawLeftFunction = std::function<float(const glm::vec2& mass_center)>;
using yawRightFunction = std::function<float(const glm::vec2& mass_center)>;

// TODO: must return something
using primaryWeaponFunction = std::function<void()>;

// returns nothing, must rotate itself
using targetFunction = std::function<void(const glm::vec2& target)>;

struct actions
{
    std::vector<forwardFunction> forward;
    std::vector<backwardFunction> backward;
    std::vector<leftFunction> left;
    std::vector<rightFunction> right;
    std::vector<yawLeftFunction> yawLeft;
    std::vector<yawRightFunction> yawRight;
    std::vector<primaryWeaponFunction> primaryWeapon;
    std::vector<targetFunction> target;
};

#endif //VOXEL_OBJECT_ACTIONS_H
