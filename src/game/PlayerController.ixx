//
// Created by A on 05.05.2026.
//
module;

#include <glm/glm.hpp>
#include <functional>

export module player_controller;

import destructible_entity;

// Простая структура для состояния ввода
// Можно заменить на polling из GLFW/SDL в месте вызова
export struct InputState
{
    bool up    = false;
    bool down  = false;
    bool left  = false;
    bool right = false;

    bool rotate_left  = false;
    bool rotate_right = false;
};

export class PlayerController
{
public:
    // Настройки управления

    PlayerController(DestructibleEntity& obj)
        : object_(obj)
    {
    }

    // Обновление физики объекта на основе ввода
    // dt - время кадра в секундах
    void update(const InputState& input)
    {
        apply_movement(input);
        apply_rotation(input);
        //apply_friction(dt);

        // Применяем изменения к трансформации объекта
        //object_.update_position(dt);
    }

private:
    DestructibleEntity& object_;

    void apply_movement(const InputState& input)
    {
        glm::vec2 force(0.0f);

        if (input.up)    force.y += 1.0f;
        if (input.down)  force.y -= 1.0f;
        if (input.right) force.x += 1.0f;
        if (input.left)  force.x -= 1.0f;

        // Нормализация вектора ввода, чтобы по диагонали не было быстрее
        if (force.x != 0.0f || force.y != 0.0f)
        {
            force = glm::normalize(force);
        }
        object_.apply_acceleration_signal(force);
    }

    void apply_rotation(const InputState& input)
    {
        float torque = 0.0f;

        if (input.rotate_right) torque += 1.0f;
        if (input.rotate_left)  torque -= 1.0f;

        object_.apply_torque_signal(torque);
    }
};
