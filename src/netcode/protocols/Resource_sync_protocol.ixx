//
// Created by A on 07.05.2026.
//
module;

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

#include "../paket_defines.h"

export module resource_sync_protocol;

export
namespace sync {

    // Максимальная длина строкового дескриптора
    constexpr size_t MAX_DESC_LENGTH = 128;
    // Максимальное количество ресурсов в одном пакете синхронизации
    constexpr size_t MAX_RESOURCES_PER_PACKET = 10;

    struct ResourceEntry {
        char descriptor[MAX_DESC_LENGTH]; // Фиксированный размер для простой сериализации
        uint32_t host_id;                 // ID на стороне хоста (unsigned int из твоего репозитория)

        ResourceEntry() : descriptor{}, host_id(0) {}

        ResourceEntry(const std::string& desc, uint32_t id) : host_id(id) {
            std::memset(descriptor, 0, MAX_DESC_LENGTH);
            std::strncpy(descriptor, desc.c_str(), MAX_DESC_LENGTH - 1);
        }

        std::string get_descriptor() const {
            return std::string(descriptor);
        }
    };

    struct ResourceSyncPacket {
        static constexpr uint32_t tcp_type_id = RESOURCES_SYNC_PACKET;
        uint32_t count;
        char8_t last;
        ResourceEntry entries[MAX_RESOURCES_PER_PACKET];

        ResourceSyncPacket() : count(0) {}
    };

} // export namespace sync