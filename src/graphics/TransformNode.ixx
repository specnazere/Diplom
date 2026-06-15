//
// Created by A on 10.06.2026.
//
module;

#include <glm/fwd.hpp>
#include <glm/vec2.hpp>

export module transform_node;

export
struct TransformNode {
    glm::vec2 position = glm::vec2(0, 0);
    glm::float32 angle = 0.0f;
    int parentIndex = -1;
};
