//
// Created by A on 12.06.2026.
//

#ifndef PHYSIC_PARAMETERS_H
#define PHYSIC_PARAMETERS_H

#include "glm/vec2.hpp"

struct kinetic_parameters
{
    float mass = 1.f;
    float moment = 1.f;
};

struct speed_parameters
{
    glm::vec2 linearVelocity = {0, 0};
    float angularVelocity = 0.f;
};

struct physic_parameters
{
    speed_parameters speed;
    kinetic_parameters kinetic;
};

#endif //PHYSIC_PARAMETERS_H
