//
// Created by A on 07.05.2026.
//
module;

#include <functional>
#include <set>
#include <string>
#include <glm/fwd.hpp>

export module host_resource_sync;

import resource_sync_protocol;
import multitype_index_repository;

import native_tcp;

// Forward declaration or import of your specific resource types if needed for iteration
// Since repository is a template, we need to know which repositories to sync.
// Assuming we sync textures, fonts, locales.

export namespace net {

    class HostResourceSynchronizer {
    public:
        explicit HostResourceSynchronizer() {}

        // Вызывать при подключении нового клиента или при изменении ресурсов
        // Для простоты считаем, что мы отправляем ВСЕ ресурсы один раз при коннекте
        // или инкрементально (тут реализована полная отправка списка)
        template<typename T>
        void sync_repository(repository<T>& repo) {
            sync::ResourceSyncPacket packet;
            packet.count = 0;
            packet.last = 0;

            // Получаем все внутренние ID
            auto ids = repo.iDescs();

            for (glm::uint32_t id : ids) {
                if (packet.count >= sync::MAX_RESOURCES_PER_PACKET) {
                    if (id == ids.back())
                        packet.last = 1;
                    // Отправляем заполненный пакет
                    send_typed(packet);
                    packet.count = 0;
                }

                // Получаем контейнер по ID
                // Внимание: operator[] в твоем репозитории принимает size_t/uint, но внутри ищет в iDescToIndex
                // Убедимся, что id валиден
                if (repo.contains(id)) {
                    const auto& container = repo[id];
                    packet.entries[packet.count++] = sync::ResourceEntry(container.sDesc, id);
                }
            }
            packet.last = 1;

            // Отправляем остаток
            if (packet.count > 0) {
                send_typed(packet);
            }


        }
    };

} // export namespace net