//
// Created by A on 07.05.2026.
//
module;

#include <glm/glm.hpp>

#include <cstdint>
#include <cstring>

#include "netcode/paket_defines.h"

export module scene_sync_protocol;

export
namespace sync {

    constexpr size_t MAX_UPDATES_PER_PACKET = 50;

    // Типы обновлений
    enum class SceneOp : uint8_t {
        Spawn   = 0,
        Update  = 1,
        Despawn = 2
    };

    // Структура одного изменения
    struct SceneEntityUpdate {
        uint32_t    index;      // ObjectHandle::index
        uint32_t    generation; // ObjectHandle::generation (чтобы понять, тот ли это объект)
        SceneOp     op;

        // Данные для Spawn/Update
        glm::vec2   position;
        float       angle;
        glm::vec2   velocity;
        float       angular_velocity;

        // Данные только для Spawn
        uint32_t    resource_id; // ID ресурса (текстуры/префаба) из ResourceSync

        SceneEntityUpdate()
            : index(0), generation(0), op(SceneOp::Update),
              position(0), angle(0), velocity(0), angular_velocity(0), resource_id(0) {}
    };

    struct SceneSyncPacket {
        static constexpr uint32_t tcp_type_id = SCENE_SYNC_PACKET;
        uint64_t            timestamp; // Тик физики хоста
        uint32_t            count;
        uint32_t            last;
        SceneEntityUpdate   updates[MAX_UPDATES_PER_PACKET];

        SceneSyncPacket() : timestamp(0), count(0) {}
    };

} // export namespace sync