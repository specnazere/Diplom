//
// Created by A on 05.05.2026.
//
module;

#include <glm/glm.hpp>
#include <functional>

export module player_controller;

import destructible_object;

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
    struct Config
    {
        float move_acceleration   = 500.0f;  // Ускорение движения
        float max_speed           = 300.0f;  // Максимальная скорость
        float rotation_acceleration = 0.1f; // Ускорение вращения (градусы/сек^2 или радианы, зависит от реализации)
        float friction            = 0.95f;   // Трение (затухание скорости)
        float angular_friction    = 0.90f;   // Трение вращения
    };

    PlayerController(DestructibleObject& obj, Config cfg = {500.f, 300.f, 1.f, 0.95f, 0.90f})
        : object_(obj)
        , config_(cfg)
    {
    }

    // Обновление физики объекта на основе ввода
    // dt - время кадра в секундах
    void update(float dt, const InputState& input)
    {
        apply_movement(input, dt);
        apply_rotation(input, dt);
        //apply_friction(dt);

        // Применяем изменения к трансформации объекта
        //object_.update_position(dt);
    }

private:
    DestructibleObject& object_;
    Config config_;

    void apply_movement(const InputState& input, float dt)
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

            // Добавляем ускорение к текущей скорости
            object_.velocity += force * config_.move_acceleration * dt;
        }
    }

    void apply_rotation(const InputState& input, float dt)
    {
        float torque = 0.0f;

        if (input.rotate_right) torque += 1.0f;
        if (input.rotate_left)  torque -= 1.0f;

        if (torque != 0.0f)
        {
            // Угловое ускорение
            object_.angular_velocity += torque * config_.rotation_acceleration * dt;
        }
    }

    void apply_friction(float dt)
    {
        // Линейное трение
        object_.velocity *= config_.friction;

        // Ограничение максимальной скорости (опционально, но полезно для стабильности)
        float speed = glm::length(object_.velocity);
        if (speed > config_.max_speed)
        {
            object_.velocity = glm::normalize(object_.velocity) * config_.max_speed;
        }

        // Угловое трение
        object_.angular_velocity *= config_.angular_friction;

        // Остановка при очень малых значениях, чтобы избежать дрожания
        if (std::abs(object_.angular_velocity) < 0.01f)
            object_.angular_velocity = 0.0f;

        if (glm::length(object_.velocity) < 0.01f)
            object_.velocity = glm::vec2(0.0f);
    }
};
