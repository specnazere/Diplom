//
// Created by A on 07.05.2026.
//
module;

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <functional>
#include <unordered_map>
#include <vector>

export module host_scene_sync;

import scene_sync_protocol;
import scene;
import native_tcp;
import scene_object;

export namespace net {

    class HostSceneSynchronizer {
    public:
        explicit HostSceneSynchronizer() {}

        // void initial_sync(Scene& scene, uint64_t current_tick)
        // {
        //     sync::SceneSyncPacket packet;
        //     packet.timestamp = current_tick;
        //     packet.count = 0;
        // }

        // Вызывать каждый тик физики (например, 30-60 раз в секунду)
        void update(Scene& scene, uint64_t current_tick) {
            sync::SceneSyncPacket packet;
            packet.timestamp = current_tick;
            packet.count = 0;

            // 1. Проверка на удаление (Despawn)
            // Итерируемся по last_known_state_ и проверяем, жив ли объект в сцене
            std::vector<uint32_t> to_remove;
            for (auto& [idx, last] : last_known_state_) {
                // Внимание: is_alive требует Handle. Мы храним только index.
                // В твоей Scene нет метода get_generation_by_index публично.
                // Допустим, мы просто проверяем, существует ли объект с таким index и generation.
                // Для простоты: если объект удален, он не вернется в for_each.

                // Чтобы корректно детектить удаление, нам нужно знать generation.
                // Давай предположим, что last.generation хранит последнее известное поколение.

                bool found = false;
                scene.for_each([&](ObjectHandle handle, const SceneObject&) {
                    if (handle.index == idx && handle.generation == last.generation) {
                        found = true;
                    }
                });

                if (!found) {
                    // Объект удален или сменил поколение (значит старый умер)
                    if (packet.count >= sync::MAX_UPDATES_PER_PACKET) {
                        send_typed(packet);
                        packet.count = 0;
                    }
                    auto& u = packet.updates[packet.count++];
                    u.index = idx;
                    u.generation = last.generation;
                    u.op = sync::SceneOp::Despawn;

                    to_remove.push_back(idx);
                }
            }
            for (auto idx : to_remove) {
                last_known_state_.erase(idx);
            }

            // 2. Проверка на создание и обновление (Spawn / Update)
            scene.for_each([&](ObjectHandle handle, const SceneObject& obj) {
                if (packet.count >= sync::MAX_UPDATES_PER_PACKET) {
                    send_typed(packet);
                    packet.count = 0;
                }

                auto it = last_known_state_.find(handle.index);

                // Если индекс занят другим объектом (другое поколение), значит старый умер, новый родился
                if (it != last_known_state_.end() && it->second.generation != handle.generation) {
                    // Старый объект удалился (уже обработано выше или будет обработано),
                    // этот считаем новым
                    it = last_known_state_.end();
                }

                bool is_new = (it == last_known_state_.end());
                bool has_changed = false;

                if (!is_new) {
                    // Проверка изменений (с порогом)
                    const auto& last = it->second;
                    float dist_sq = glm::distance2(last.position, obj.get_position());
                    float angle_diff = std::abs(last.angle - obj.get_angle());

                    // Пороги: 1см и 1 градус
                    if (dist_sq > 0.0001f || angle_diff > 0.017f ||
                        glm::length(obj.get_velocity() - last.velocity) > 0.01f) {
                        has_changed = true;
                    }
                }

                if (is_new || has_changed) {
                    auto& u = packet.updates[packet.count++];
                    u.index = handle.index;
                    u.generation = handle.generation;
                    u.position = obj.get_position();
                    u.angle = obj.get_angle();
                    u.velocity = obj.get_velocity(); // Предположим, что velocity публичный или есть геттер
                    u.angular_velocity = obj.get_angular_velocity();

                    if (is_new) {
                        u.op = sync::SceneOp::Spawn;
                        u.resource_id = obj.get_parent_texture();
                    } else {
                        u.op = sync::SceneOp::Update;
                    }

                    // Обновляем теневое состояние
                    ShadowState shadow;
                    shadow.generation = handle.generation;
                    shadow.position = u.position;
                    shadow.angle = u.angle;
                    shadow.velocity = u.velocity;
                    shadow.angular_velocity = u.angular_velocity;

                    last_known_state_[handle.index] = shadow;
                }
            });

            if (packet.count > 0) {
                send_typed(packet);
            }
        }

    private:
        struct ShadowState {
            uint32_t generation;
            glm::vec2 position;
            float angle;
            glm::vec2 velocity;
            float angular_velocity;
        };

        std::unordered_map<uint32_t, ShadowState> last_known_state_;
    };

} // export namespace net