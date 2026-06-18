//
// Created by A on 17.06.2026.
//
module;

#include <vector>
#include <functional>
#include <unordered_map>
#include <deque>
#include <optional>
#include <cstring>
#include <iostream>
#include <string_view>

export module client_receiver;

export import voxel_health_protocol;
export import native_tcp;

export
class ClientReceiver {
public:
    using SnapshotCallback = std::function<void(
        uint32_t object_id,
        uint32_t cols,
        uint32_t rows,
        std::string texture_name,
        const std::vector<VoxelStateUpdate>& updates
    )>;

    using DamageCallback = std::function<void(const VoxelBatchUpdate& batch)>;

private:
    struct PendingSnapshot {
        uint32_t cols = 0;
        uint32_t rows = 0;
        std::string texture_name;
        uint32_t total_count = 0;
        std::vector<VoxelStateUpdate> received_updates;
    };

    std::unordered_map<uint32_t, PendingSnapshot> pending_snapshots_;

    SnapshotCallback on_snapshot_complete_;
    DamageCallback on_damage_batch_;

public:
    ClientReceiver(SnapshotCallback snapshot_cb, DamageCallback damage_cb)
        : on_snapshot_complete_(std::move(snapshot_cb))
        , on_damage_batch_(std::move(damage_cb))
    {}

    void process_network_packets() {
        try {
            net::UnknownPacket packet = net::receive_unknown(1);

            switch (packet.type_id) {
                case 2: { // VoxelSnapshotMetadata
                    if (packet.payload.size() == sizeof(VoxelSnapshotMetadata)) {
                        VoxelSnapshotMetadata meta;
                        std::memcpy(&meta, packet.payload.data(), sizeof(meta));

                        auto& pending = pending_snapshots_[meta.object_id];
                        pending.cols = meta.cols;
                        pending.rows = meta.rows;
                        pending.texture_name = std::string(meta.texture_name);
                        pending.total_count = meta.total_voxel_count;
                        pending.received_updates.clear();
                        pending.received_updates.reserve(meta.total_voxel_count);
                    }
                    break;
                }
                case 3: { // VoxelSnapshotChunk
                    if (packet.payload.size() == sizeof(VoxelSnapshotChunk)) {
                        VoxelSnapshotChunk chunk;
                        std::memcpy(&chunk, packet.payload.data(), sizeof(chunk));

                        auto& pending = pending_snapshots_[chunk.object_id];

                        // Защита от чанков без метаданных
                        if (pending.received_updates.capacity() == 0) break;

                        for (uint32_t i = 0; i < chunk.count; ++i) {
                            pending.received_updates.push_back(chunk.updates[i]);
                        }

                        if (pending.received_updates.size() >= pending.total_count) {
                            on_snapshot_complete_(
                                chunk.object_id,
                                pending.cols,
                                pending.rows,
                                pending.texture_name,
                                pending.received_updates
                            );
                            pending_snapshots_.erase(chunk.object_id);
                        }
                    }
                    break;
                }
                case 1: { // VoxelBatchUpdate (урон)
                    if (packet.payload.size() == sizeof(VoxelBatchUpdate)) {
                        VoxelBatchUpdate batch;
                        std::memcpy(&batch, packet.payload.data(), sizeof(batch));
                        on_damage_batch_(batch);
                    }
                    break;
                }
                default:
                    std::cerr << "Unknown packet type: " << packet.type_id << "\n";
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