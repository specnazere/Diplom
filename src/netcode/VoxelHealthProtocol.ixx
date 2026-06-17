//
// Created by A on 16.06.2026.
//
module;

#include <cstdint>
#include <type_traits>

export module voxel_health_protocol;

// Сетевой статус, чтобы не тащить зависимости из voxel_object
export
enum class NetVoxelStatus : uint8_t {
    Absent   = 0,
    Active   = 1,
    Boundary = 2,
    Destroyed = 3
};

// Обновление одного вокселя. 16 байт, идеально выровнено.
export
struct alignas(8) VoxelStateUpdate {
    uint32_t voxel_idx;
    float    health;
};
static_assert(std::is_trivially_copyable_v<VoxelStateUpdate>);
static_assert(sizeof(VoxelStateUpdate) == 8);

// Батч-пакет. Отправляем его через send_typed.
export
struct VoxelBatchUpdate {
    static constexpr uint32_t tcp_type_id = 1; // Требование твоего native_tcp

    uint32_t object_id;      // ID объекта на сцене, чтобы понять, кому применять
    uint32_t count;          // Реальное количество обновлений в батче (<= 64)
    VoxelStateUpdate updates[128];
};
static_assert(std::is_trivially_copyable_v<VoxelBatchUpdate>);

// В модуль voxel_health_protocol

// 1. Метаданные (отправляется ОДИН раз при инициализации)
export
struct VoxelSnapshotMetadata {
    static constexpr uint32_t tcp_type_id = 2;

    uint32_t object_id;
    uint32_t cols;
    uint32_t rows;
    uint32_t total_voxel_count; // Чтобы клиент знал, когда сборка завершена
    char texture_name[64];
};
static_assert(std::is_trivially_copyable_v<VoxelSnapshotMetadata>);

// 2. Чанк данных (может быть отправлено несколько раз)
export
struct VoxelSnapshotChunk {
    static constexpr uint32_t tcp_type_id = 3;

    uint32_t object_id;
    uint32_t offset; // Смещение в общем массиве вокселей
    uint32_t count;  // Количество валидных элементов в updates
    VoxelStateUpdate updates[512];
};
static_assert(std::is_trivially_copyable_v<VoxelSnapshotChunk>);