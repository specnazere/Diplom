//
// Created by A on 18.05.2026.
//

#ifndef VOXEL_OBJECT_ACTIONS_H
#define VOXEL_OBJECT_ACTIONS_H

#include <functional>
#include <optional>
#include <glm/vec2.hpp>

import projectile;

enum action
{
    forward = 0,
    backward,
    left,
    right,
    yawLeft,
    yawRight,
    primaryWeapon,
    targetAim,
};

// returns push
using forward_f = std::function<float(glm::vec2 mass_center)>;
using backward_f = std::function<float(glm::vec2 mass_center)>;
using left_f = std::function<float(glm::vec2 mass_center)>;
using right_f = std::function<float(glm::vec2 mass_center)>;

// returns torque
using yawLeft_f = std::function<float(glm::vec2 mass_center)>;
using yawRight_f = std::function<float(glm::vec2 mass_center)>;

// returns projectile makeup data
using primaryWeapon_f = std::function<std::optional<projectile_makeup_data>(float dt)>;

// returns nothing, must rotate itself
using target_f = std::function<void(glm::vec2 target)>;

struct actions
{
    std::vector<forward_f> forward;
    std::vector<backward_f> backward;
    std::vector<left_f> left;
    std::vector<right_f> right;
    std::vector<yawLeft_f> yawLeft;
    std::vector<yawRight_f> yawRight;
    std::vector<primaryWeapon_f> primaryWeapon;
    std::vector<target_f> target;
};

#endif //VOXEL_OBJECT_ACTIONS_H
