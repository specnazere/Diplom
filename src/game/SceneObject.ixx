//
// Created by A on 11.06.2026.
//
module;

#include <memory>

export module scene_object;

import physic_object;
import scene_event;
import destructible_object;

export
class SceneObject : public PhysicObject, public DestructibleObject
{
public:
    virtual std::unique_ptr<events::SceneEvent> on_collision() = 0;
    virtual const size_t& get_parent_texture() const = 0;
};
