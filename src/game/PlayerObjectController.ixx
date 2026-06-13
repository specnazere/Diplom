//
// Created by A on 05.05.2026.
//
module;

#include <glm/glm.hpp>
#include <functional>

export module player_object_controller;

import destructible_entity;
import camera;

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

    bool fire = false;

    glm::vec2 globalCursorPosition{};
};

export class PlayerObjectController
{
private:
    ModuledEntity& object_;

public:
    // Настройки управления

    PlayerObjectController(ModuledEntity& obj)
        : object_(obj)
    {}

    // Обновление физики объекта на основе ввода
    // dt - время кадра в секундах
    void update(const InputState& input)
    {
        signals::signal_t& signal = object_.signal;
        signal = 0;

        if (input.up)    signal += signals::forwardSignal;
        if (input.down)  signal += signals::backwardSignal;
        if (input.right) signal += signals::rightSignal;
        if (input.left)  signal += signals::leftSignal;
        if (input.rotate_right) signal += signals::yawLeftSignal;
        if (input.rotate_left)  signal += signals::yawRightSignal;
        if (input.fire)
            signal += signals::primaryWeaponSignal;

        object_.target = input.globalCursorPosition;
    }
};
