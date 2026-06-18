//
// Created by A on 18.06.2026.
//
module;

#include <glm/vec2.hpp>

export module input_state;

export struct InputState
{
    bool up    = false;
    bool down  = false;
    bool left  = false;
    bool right = false;

    bool rotate_left  = false;
    bool rotate_right = false;

    bool fire = false;

    glm::vec2 globalCursorPosition{};
};