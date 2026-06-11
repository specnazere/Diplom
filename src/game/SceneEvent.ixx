//
// Created by A on 10.06.2026.
//
module;

#include <glm/glm.hpp>

#include "explosion_data.h"

export module scene_event;

namespace events
{
    export
    enum eventType
    {
        emptyEvent = 0,
        explosionEvent
    };

    export
    struct SceneEvent
    {
        virtual ~SceneEvent() {}
        virtual eventType type() {return emptyEvent;}
    };

    export
    struct SpawnExplosion : SceneEvent
    {
        explosion_data explosion;

        eventType type() override {
            return explosionEvent;
        }
    };
}