//
// Created by A on 07.05.2026.
//
module;

#include <unordered_map>
#include <cstdint>
#include <functional>
#include <iostream>
#include <set>

#include "paket_defines.h"

export module client_resource_sync;

import resource_sync_protocol;
import multitype_index_repository;
import native_tcp;

export
namespace net {

    class ClientResourceSynchronizer {
    public:
        // Callback, который вызывается, когда клиент узнает о новом ресурсе от хоста.
        // Аргументы: string_descriptor, host_id
        // Внутри этого коллбэка клиент должен убедиться, что ресурс загружен в его локальный репозиторий,
        // и вернуть локальный ID (или просто выполнить загрузку).
        using OnResourceRequired = std::function<void(const std::string& descriptor, uint32_t host_id)>;

        explicit ClientResourceSynchronizer(OnResourceRequired on_required)
            : on_required_(std::move(on_required))
        {}

        // Вызывать при получении пакета от хоста
        void on_receive(const sync::ResourceSyncPacket& packet) {
            for (uint32_t i = 0; i < packet.count; ++i) {
                const auto& entry = packet.entries[i];
                std::string desc = entry.get_descriptor();
                uint32_t host_id = entry.host_id;

                // Сохраняем маппинг: Host ID -> (пока ничего, или можно сохранить desc)
                // Основная задача здесь - убедиться, что ресурс есть у клиента.

                // Проверяем, знаем ли мы уже этот хост ID
                if (host_to_local_map_.find(host_id) != host_to_local_map_.end()) {
                    continue; // Уже синхронизировано
                }

                // Уведомляем систему ресурсов клиента
                if (on_required_) {
                    on_required_(desc, host_id);
                }

                // ВАЖНО:
                // В этой точке клиент должен иметь загруженный ресурс в своем репозитории.
                // Предположим, что on_required_ гарантирует, что ресурс добавлен в локальный репозиторий.
                // Нам нужно получить ЛОКАЛЬНЫЙ ID этого ресурса, чтобы сохранить маппинг.

                // Проблема: функция on_required_ void.
                // Нам нужно изменить дизайн или предположить, что клиентский репозиторий использует ТОТ ЖЕ строковый ключ.
                // Если клиентский репозиторий тоже использует string descriptor для добавления,
                // то локальный ID может отличаться от хостового.

                // Поэтому мы должны запросить у клиента его локальный ID по строке.
                // Но этот модуль не знает о репозитории напрямую (чтобы быть универсальным).

                // Решение: Добавим метод register_mapping, который вызывается ВНЕШНИМ кодом после загрузки ресурса.
                // Или передадим репозиторий внутрь. Давай сделаем проще:
                // Этот модуль просто хранит карту. Внешний код должен сказать нам: "Этому хост ID соответствует этот локальный ID".

                // Пока просто сохраним факт получения, а маппинг обновим отдельно.
                received_host_ids_.insert(host_id);

                std::cout << "Received resource info: " << desc << " (Host ID: " << host_id << ")" << std::endl;
            }
        }

        // Получить локальный ID по Host ID
        // Возвращает 0 или exception, если маппинга нет
        uint32_t get_local_id(uint32_t host_id) const {
            auto it = host_to_local_map_.find(host_id);
            if (it != host_to_local_map_.end()) {
                return it->second;
            }
            return static_cast<uint32_t>(-1); // Invalid ID
        }

        // Получить Host ID по локальному ID (для отправки действий на хост)
        uint32_t get_host_id(uint32_t local_id) const {
            auto it = local_to_host_map_.find(local_id);
            if (it != local_to_host_map_.end()) {
                return it->second;
            }
            return static_cast<uint32_t>(-1);
        }

        // Регистрация соответствия. Вызывать после того, как ресурс загружен на клиенте.
        void register_mapping(uint32_t host_id, uint32_t local_id) {
            host_to_local_map_[host_id] = local_id;
            local_to_host_map_[local_id] = host_id;
        }

        void load_resources_mapping()
        {
            sync::ResourceSyncPacket packet;
            while (packet.last == 0) {
                packet = net::receive_typed<sync::ResourceSyncPacket>(1000);
                on_receive(packet);
            }
        }

        private:
            OnResourceRequired on_required_;
            std::unordered_map<uint32_t, uint32_t> host_to_local_map_;
            std::unordered_map<uint32_t, uint32_t> local_to_host_map_;
            std::set<uint32_t> received_host_ids_;
        };

} // export namespace net