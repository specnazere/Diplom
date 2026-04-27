module;

#include "glm/glm.hpp"
#include "graphics/uv_rect.h"

#include <vector>
#include <algorithm>
#include <utility>

export module destructible_object;

import sprite;
import sprite_repository;
import texture;
import atlas;

// ---------------------------------------------------------------------------
//  Статусы вокселей
// ---------------------------------------------------------------------------
export enum class VoxelStatus : std::uint8_t
{
    Absent   = 0,  // пустая ячейка (прозрачный пиксель или разрушена)
    Active   = 1,  // живой воксель
};

// ---------------------------------------------------------------------------
//  Воксель — один пиксель текстуры, отображённый как квадратный спрайт.
//  Никаких смещений. Позиция = center, размер = cell_size x cell_size.
// ---------------------------------------------------------------------------
export struct Voxel
{
    glm::vec2   center;                    // позиция в мировых координатах
    float       hp     = 1.f;
    VoxelStatus status = VoxelStatus::Absent;
    size_t      spriteIdx = ~size_t{0};

    bool is_alive()   const { return status == VoxelStatus::Active && hp > 0.f; }
    bool has_sprite() const { return spriteIdx != ~size_t{0}; }
};

// ---------------------------------------------------------------------------
//  DestructibleObject
//
//  Сетка вокселей строится прямо по пикселям текстуры:
//  каждый непрозрачный пиксель (alpha > 0) — один спрайт cell_size x cell_size.
//  UV каждого спрайта = ровно один пиксель текстуры.
// ---------------------------------------------------------------------------
export class DestructibleObject
{
public:
    struct Params
    {
        float cell_size  = 1.f;   // размер одной ячейки в мировых единицах
        float default_hp = 1.f;
        float depth      = 0.f;
        float paralax    = 1.f;
        glm::vec2 origin = {0.f, 0.f};  // мировая позиция левого нижнего угла сетки
    };

    // tex      — текстура; colorData[i*4+3] — альфа-канал пикселя i
    // Если текстура без альфа-канала (normaleData и т.д.) — передайте alpha_threshold = 0,
    // тогда все пиксели считаются непрозрачными.
    DestructibleObject(
        SpritesRepository& repository,
        const texture&     tex,
        const uv_rect& local_UV,
        Params             params = {1, 1, 0, 1, {0, 0}},
        unsigned char      alpha_threshold = 0)
        : repo_(repository)
        , tex_(tex)
        , params_(params)
        , local_UV_(local_UV)
    {
        build_voxels(alpha_threshold);
    }

    DestructibleObject(const DestructibleObject&)            = delete;
    DestructibleObject& operator=(const DestructibleObject&) = delete;
    DestructibleObject(DestructibleObject&&)                 = default;
    DestructibleObject& operator=(DestructibleObject&&)      = default;

    ~DestructibleObject()
    {
        for (auto& vx : voxels_)
            if (vx.has_sprite())
                repo_.removeSprite(vx.spriteIdx);
    }

    // -----------------------------------------------------------------------
    //  Доступ
    // -----------------------------------------------------------------------
    const std::vector<Voxel>& voxels() const { return voxels_; }
    int grid_cols()                     const { return cols_; }
    int grid_rows()                     const { return rows_; }

    const Voxel* voxel_at(int col, int row) const
    {
        if (col < 0 || col >= cols_ || row < 0 || row >= rows_) return nullptr;
        return &voxels_[static_cast<size_t>(row * cols_ + col)];
    }
    Voxel* voxel_at(int col, int row)
    {
        return const_cast<Voxel*>(std::as_const(*this).voxel_at(col, row));
    }

    // -----------------------------------------------------------------------
    //  Нанести урон. Возвращает true если хотя бы один воксель уничтожен.
    // -----------------------------------------------------------------------
    bool apply_damage(const glm::vec2& world_pos, float damage, float radius = 0.f)
    {
        bool changed = false;
        const float cs = params_.cell_size;

        int start_col = static_cast<int>((world_pos.x - radius - params_.origin.x + tex_.width * cs / 2) / cs);
        int end_col   = static_cast<int>((world_pos.x + radius - params_.origin.x + tex_.width * cs / 2) / cs) + 1;
        int start_row = static_cast<int>((world_pos.y - radius - params_.origin.y + tex_.height * cs / 2) / cs);
        int end_row   = static_cast<int>((world_pos.y + radius - params_.origin.y + tex_.height * cs / 2) / cs) + 1;

        start_col = std::max(0, start_col);
        end_col   = std::min(cols_, end_col);
        start_row = std::max(0, start_row);
        end_row   = std::min(rows_, end_row);

        for (int r = start_row; r < end_row; ++r)
        {
            for (int c = start_col; c < end_col; ++c)
            {
                Voxel& vx = voxels_[static_cast<size_t>(r * cols_ + c)];
                if (vx.status == VoxelStatus::Absent) continue;

                if (radius > 0.f)
                {
                    glm::vec2 diff = vx.center - world_pos;
                    if (diff.x * diff.x + diff.y * diff.y > radius * radius) continue;
                }

                vx.hp -= damage;
                if (vx.hp <= 0.f)
                {
                    vx.hp = 0.f;
                    destroy_voxel(vx);
                    changed = true;
                }
            }
        }

        return changed;
    }

    // -----------------------------------------------------------------------
    //  Сдвинуть позицию всех спрайтов (если не используется TransformNode)
    // -----------------------------------------------------------------------
    void sync_sprites(glm::vec2 world_offset = {0.f, 0.f})
    {
        for (auto& vx : voxels_)
        {
            if (!vx.has_sprite()) continue;
            SpriteData& sd = repo_.dataFromSpriteIdx(vx.spriteIdx);
            sd.position_x = vx.center.x + world_offset.x;
            sd.position_y = vx.center.y + world_offset.y;
        }
    }

private:
    void build_voxels(unsigned char alpha_threshold)
    {
        cols_ = tex_.width;
        rows_ = tex_.height;

        if (cols_ <= 0 || rows_ <= 0) return;

        voxels_.resize(static_cast<size_t>(cols_ * rows_));

        const float cs      = params_.cell_size;
        const float pixel_u = (local_UV_.u1 - local_UV_.u0) / static_cast<float>(cols_);
        const float pixel_v = (local_UV_.v1 - local_UV_.v0) / static_cast<float>(rows_);
        const size_t tex_desc = reinterpret_cast<size_t>(&tex_);

        for (int row = 0; row < rows_; ++row)
        {
            for (int col = 0; col < cols_; ++col)
            {
                Voxel& vx = voxels_[static_cast<size_t>(row * cols_ + col)];

                vx.center = {
                    params_.origin.x + (col + 0.5f - cols_ / 2.f) * cs,
                    params_.origin.y + (row + 0.5f - rows_ / 2.f) * cs
                };
                vx.hp     = params_.default_hp;
                vx.status = VoxelStatus::Absent;

                // Проверяем альфа-канал пикселя (colorData: RGBA, 4 байта на пиксель)
                if (tex_.colorData != nullptr)
                {
                    const size_t pixel_idx = static_cast<size_t>(row * cols_ + col);
                    const unsigned char alpha = tex_.colorData[pixel_idx * 4 + 3];
                    if (alpha <= alpha_threshold) continue;
                }

                vx.status = VoxelStatus::Active;

                // UV: центр ровно одного пикселя
                SpriteData sd;
                sd.width      = cs;
                sd.height     = cs;
                sd.angle      = 0.f;
                sd.position_x = vx.center.x;
                sd.position_y = vx.center.y;
                sd.paralax    = params_.paralax;
                sd.uvRect.u0  = local_UV_.u0 + static_cast<float>(col) * pixel_u;        // левый край
                sd.uvRect.v0  = local_UV_.v0 + static_cast<float>(row) * pixel_v;        // верхний край
                sd.uvRect.u1  = sd.uvRect.u0 + pixel_u;
                sd.uvRect.v1  = sd.uvRect.v0 + pixel_v;

                vx.spriteIdx = repo_.addSprite(sd, params_.depth);
                repo_[vx.spriteIdx].textureDesc = tex_desc;
            }
        }
    }

    void destroy_voxel(Voxel& vx)
    {
        if (vx.has_sprite())
        {
            repo_.removeSprite(vx.spriteIdx);
            vx.spriteIdx = ~size_t{0};
        }
        vx.status = VoxelStatus::Absent;
    }

    SpritesRepository& repo_;
    const texture&     tex_;
    Params             params_;

    std::vector<Voxel> voxels_;
    int cols_ = 0;
    int rows_ = 0;

    uv_rect local_UV_;
};
