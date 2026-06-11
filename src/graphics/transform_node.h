//
// Created by A on 10.06.2026.
//

#ifndef TRANSFORM_NODE_H
#define TRANSFORM_NODE_H

#include <glm/glm.hpp>

struct TransformNode {
    glm::vec2 position = glm::vec2(0, 0);
    glm::float32 angle = 0.0f;
    int parentIndex = -1;
};

#endif //TRANSFORM_NODE_H
