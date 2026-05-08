//
// Created by A on 08.05.2026.
//
module;

#include <cstdint>

#include "../paket_defines.h"

export module atlas_sync_protocol;

export
namespace sync
{
    constexpr size_t MAX_ID_PER_PACKET = 256;

    struct AtlasSyncPacket
    {
        static constexpr uint32_t tcp_type_id = ATLAS_SYNC_PACKET;
        uint32_t count;
        uint32_t last;
        uint32_t ids[MAX_ID_PER_PACKET];
    };
}