//
// Created by A on 05.05.2026.
//
module;

#include <glm/glm.hpp>
#include <functional>

export module player_object_controller;

export import moduled_entity;
export import networked_voxel_object;
export import camera;
export import input_state;

export class PlayerObjectController
{
private:
    ModuledEntity<NetworkedVoxelObject>& object_;

public:
    // Настройки управления

    PlayerObjectController(ModuledEntity<NetworkedVoxelObject>& obj)
        : object_(obj)
    {}

    // Обновление физики объекта на основе ввода
    // dt - время кадра в секундах
    void update(const InputState& playerInput)
    {
        signals::signal_t& signal = object_.signal;
        signal = 0;

        if (playerInput.up)    signal += signals::forwardSignal;
        if (playerInput.down)  signal += signals::backwardSignal;
        if (playerInput.right) signal += signals::rightSignal;
        if (playerInput.left)  signal += signals::leftSignal;
        if (playerInput.rotate_right) signal += signals::yawLeftSignal;
        if (playerInput.rotate_left)  signal += signals::yawRightSignal;
        if (playerInput.fire) signal += signals::primaryWeaponSignal;

        object_.target = playerInput.globalCursorPosition;

        glm::vec2 object_world_pos = object_.get_object().global_position();
        camera::set_pos(object_world_pos.x, object_world_pos.y);
    }
};
