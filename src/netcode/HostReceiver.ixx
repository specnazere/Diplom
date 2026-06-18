//
// Created by A on 18.06.2026.
//
// Новый модуль host_receiver (или добавь в существующий)
module;

#include <functional>
#include <cstring>
#include <iostream>
#include <string_view>

export module host_receiver;

export import voxel_health_protocol;
export import native_tcp;

export class HostReceiver {
public:
    using HealCommandCallback = std::function<void(const VoxelHealCommand& cmd)>;

private:
    HealCommandCallback on_heal_command_;

public:
    explicit HostReceiver(HealCommandCallback callback)
        : on_heal_command_(std::move(callback)) {}

    void process_commands() {
        try {
            net::UnknownPacket packet = net::receive_unknown(1);

            switch (packet.type_id) {
                case 4: { // VoxelHealCommand
                    if (packet.payload.size() == sizeof(VoxelHealCommand)) {
                        VoxelHealCommand cmd;
                        std::memcpy(&cmd, packet.payload.data(), sizeof(cmd));
                        on_heal_command_(cmd);
                    }
                    break;
                }
                default:
                    std::cerr << "Unknown command type: " << packet.type_id << "\n";
                    break;
            }
        } catch (const std::runtime_error& e) {
            std::string_view err(e.what());
            if (err.find("10060") == std::string_view::npos) {
                std::cerr << "Network error: " << e.what() << "\n";
            }
        }
    }
};
