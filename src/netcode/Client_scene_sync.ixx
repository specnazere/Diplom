//
// Created by A on 07.05.2026.
//
module;

#include <glm/glm.hpp>
#include <functional>
#include <unordered_map>
#include <iostream>
#include <memory>

export module client_scene_sync;

import scene_sync_protocol;
import scene;
import resources;
import destructible_object;
import native_tcp;
import sprite_repository;

export
namespace net {

    class ClientSceneSynchronizer {
    public:
        ClientSceneSynchronizer(Scene& local_scene, Resources& resources, SpritesRepository& sprites_repository)
            : local_scene_(local_scene)
            , resources_(resources)
            , sprites_repo_(sprites_repository)
        {}

        void read_all()
        {
            sync::SceneSyncPacket sync_packet;
            while (true) {
                sync_packet = net::receive_typed<sync::SceneSyncPacket>(10000);
                if (sync_packet.count == 0)
                    break;
                else
                    on_receive(sync_packet);
            }
        }

        void on_receive(const sync::SceneSyncPacket& packet) {
            for (uint32_t i = 0; i < packet.count; ++i) {
                const auto& u = packet.updates[i];

                // Ключ для мапы: комбинация index и generation уникальна для "жизни" объекта
                uint64_t key = (static_cast<uint64_t>(u.generation) << 32) | u.index;

                switch (u.op) {
                    case sync::SceneOp::Spawn: {
                        // Создаем объект
                        ObjectHandle handle = spawn(u.resource_id, u.position, u.angle);
                        if (handle.is_valid()) {
                            client_handles_[key] = handle;

                            // Инициализируем физику (скорости)
                            if (auto* obj = local_scene_.get(handle)) {
                                obj->velocity = u.velocity;
                                obj->angular_velocity = u.angular_velocity;
                            }
                        }
                        break;
                    }
                    case sync::SceneOp::Update: {
                        auto it = client_handles_.find(key);
                        if (it != client_handles_.end()) {
                            ObjectHandle handle = it->second;
                            if (auto* obj = local_scene_.get(handle)) {
                                // Вариант А: Жесткая установка (телепорт)
                                // obj->set_position(u.position);
                                // obj->set_rotation(u.angle);

                                // Вариант Б: Сохранение для интерполяции (рекомендуется)
                                // Здесь нужно сохранять в HistoryBuffer, который рендерер будет читать
                                // Для простоты пока запишем в объект, но в реальности нужно писать в SyncBuffer
                                obj->velocity = u.velocity; // Для экстраполяции
                                // Позицию лучше интерполировать в render loop
                            }
                        }
                        break;
                    }
                    case sync::SceneOp::Despawn: {
                        auto it = client_handles_.find(key);
                        if (it != client_handles_.end()) {
                            despawn(it->second);
                            client_handles_.erase(it);
                        }
                        break;
                    }
                }
            }
        }

    private:
        Scene& local_scene_;
        Resources& resources_;
        SpritesRepository& sprites_repo_;

        // Маппинг: (Generation << 32 | Index) -> Local ObjectHandle
        std::unordered_map<uint64_t, ObjectHandle> client_handles_;

        ObjectHandle spawn(size_t texture_idx, glm::vec2 position, float angle)
        {
            auto handler = local_scene_.add(std::make_unique<DestructibleObject>(
                    sprites_repo_,
                    resources_.texturesRepo[texture_idx].object, texture_idx,
                    sprites_repo_.atlas.get_uv(texture_idx),
                    DestructibleObject::Params{5, 5, 0, 1, position}
                ));
            // scene.get({})->set_rotation(angle);
            return handler;
        }

        void despawn(ObjectHandle handle) {
            local_scene_.remove(handle);
        }
    };

} // export namespace net