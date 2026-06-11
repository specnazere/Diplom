//
// Created by A on 11.06.2026.
//

#ifndef EXPLOSION_DATA_H
#define EXPLOSION_DATA_H

#include <glm/vec2.hpp>

struct explosion_data
{
    float radius;
    glm::vec2 position;
    float strength;
    float impulse;
};

#endif //EXPLOSION_DATA_H
