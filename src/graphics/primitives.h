//
// Created by A on 28.01.2026.
//

#ifndef PRIMITIVES_H
#define PRIMITIVES_H

#include <glm/fwd.hpp>

constexpr glm::float32 square_vertices[] = {
    1.0f,  1.0f,
   -1.0f,  1.0f,
   -1.0f, -1.0f,
    1.0f, -1.0f,
};

constexpr glm::float32 textured_square_vertices[] = {
     1.0f,  1.0f, 0.0f, 0.0f,
    -1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 1.0f, 1.0f,
     1.0f, -1.0f, 0.0f, 1.0f,
};

constexpr glm::uint square_indices[] = {
    0, 1, 2,
    2, 3, 0,
};

#endif //PRIMITIVES_H
