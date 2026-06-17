//
// Created by A on 17.06.2026.
//
module;

#include "graphics/uv_rect.h"
#include "voxel_object_params.h"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

export module networked_voxel_object;

export import voxel_object;
export import voxel_health_protocol;
export import native_tcp;
export import host_sender;

export
class NetworkedVoxelObject : public VoxelObject
{
private:
    uint32_t network_id_;
    std::vector<VoxelStateUpdate> dirty_buffer_;
    std::string texture_name_;

public:
    NetworkedVoxelObject(
        uint32_t net_id,
        const texture& tex,
        const size_t texture_id,
        const std::string& tex_name,
        const uv_rect& local_UV,
        const voxel_object_params& params = standart_params,
        bool chipped = false,
        unsigned char alpha_threshold = 0)
        : VoxelObject(tex, texture_id, local_UV, params, chipped, alpha_threshold)
        , network_id_(net_id)
        , texture_name_(tex_name)
    {
        dirty_buffer_.reserve(128);
    }

    uint32_t get_network_id() const { return network_id_; }

protected:
    // Переопределяем хук базового класса
    void on_voxel_damaged(int col, int row) override
    {
        const Voxel* vx = voxel_at(col, row);
        if (!vx) return;

        VoxelStateUpdate upd;
        upd.voxel_idx = static_cast<uint32_t>(row * grid_cols() + col);
        upd.health = vx->hp;

        dirty_buffer_.push_back(upd);
    }

public:
    // Вызывается в сетевом тике (например, из GameLoop)
    std::optional<VoxelBatchUpdate> get_voxel_batch()
    {
        if (dirty_buffer_.empty())
            return std::nullopt;

        VoxelBatchUpdate batch{};
        batch.object_id = network_id_;

        batch.count = static_cast<uint32_t>(std::min(dirty_buffer_.size(), static_cast<size_t>(128)));

        for (uint32_t i = 0; i < batch.count; ++i) {
            batch.updates[i] = dirty_buffer_[i];
        }

        if (dirty_buffer_.size() > 128) {
            dirty_buffer_.erase(dirty_buffer_.begin(), dirty_buffer_.begin() + 128);
        } else {
            dirty_buffer_.clear();
        }

        return batch;
    }

    void send_full_snapshot() const
    {
        std::vector<VoxelStateUpdate> active_voxels;
        active_voxels.reserve(grid_cols() * grid_rows());

        for (int r = 0; r < grid_rows(); ++r) {
            for (int c = 0; c < grid_cols(); ++c) {
                const Voxel* vx = voxel_at(c, r);
                if (vx && vx->is_alive()) {
                    active_voxels.push_back({
                        static_cast<uint32_t>(r * grid_cols() + c),
                        vx->hp
                    });
                }
            }
        }

        uint32_t total = static_cast<uint32_t>(active_voxels.size());

        // 1. Отправляем метаданные
        VoxelSnapshotMetadata meta{};
        meta.object_id = network_id_;
        meta.cols = static_cast<uint32_t>(grid_cols());
        meta.rows = static_cast<uint32_t>(grid_rows());
        meta.total_voxel_count = total;

        const std::string& tex_name = texture_name_;
        std::strncpy(meta.texture_name, tex_name.c_str(), sizeof(meta.texture_name) - 1);
        meta.texture_name[sizeof(meta.texture_name) - 1] = '\0';

        net::send_typed(meta);

        // 2. Отправляем чанки
        constexpr uint32_t chunk_capacity = 512;
        for (uint32_t i = 0; i < total; i += chunk_capacity) {
            VoxelSnapshotChunk chunk{};
            chunk.object_id = network_id_;
            chunk.offset = i;
            chunk.count = std::min(chunk_capacity, total - i);

            for (uint32_t j = 0; j < chunk.count; ++j) {
                chunk.updates[j] = active_voxels[i + j];
            }
            net::send_typed(chunk);
        }
    }
};