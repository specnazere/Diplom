module;

#include "glm/glm.hpp"
#include "graphics/uv_rect.h"

#include <vector>
#include <algorithm>
#include <utility>
#include <queue>
#include <limits>

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
//  Позиция центрирована вокруг origin объекта: (col + 0.5 - cols/2) * cell_size.
//  Никаких случайных или граничных смещений.
// ---------------------------------------------------------------------------
export struct Voxel
{
    glm::vec2   center;
    float       hp     = 1.f;
    VoxelStatus status = VoxelStatus::Absent;
    size_t      spriteIdx = ~size_t{0};

    bool is_alive()   const { return status == VoxelStatus::Active && hp > 0.f; }
    bool has_sprite() const { return spriteIdx != ~size_t{0}; }
};

// ---------------------------------------------------------------------------
//  DestructibleObject
//
//  Сетка вокселей 1:1 по пикселям sub-региона текстуры (local_UV).
//  Непрозрачные пиксели (alpha > alpha_threshold) → активный воксель-спрайт.
//
//  После apply_damage можно вызвать split_if_disconnected() — метод
//  проверяет 4-связность сетки и возвращает список отколовшихся фрагментов
//  (каждый — новый DestructibleObject с теми же параметрами, но только
//  со своим подмножеством вокселей; спрайты перемещаются, не пересоздаются).
// ---------------------------------------------------------------------------
export class DestructibleObject
{
public:
    struct Params
    {
        float cell_size  = 1.f;  // размер одной ячейки в мировых единицах
        float default_hp = 1.f;
        float depth      = 0.f;
        float paralax    = 1.f;
        // Мировая позиция центра объекта (сетка симметрична вокруг этой точки)
        glm::vec2 origin = {0.f, 0.f};
    };

    // -----------------------------------------------------------------------
    //  Конструктор
    //
    //  repository       — репозиторий спрайтов
    //  tex              — текстура (colorData: RGBA, 4 байта на пиксель)
    //  local_UV         — UV-прямоугольник региона атласа [u0,v0]–[u1,v1]
    //  params           — параметры объекта
    //  alpha_threshold  — пиксели с alpha <= порога считаются пустыми;
    //                     при 0 (по умолчанию) все пиксели активны
    // -----------------------------------------------------------------------
    DestructibleObject(
        SpritesRepository& repository,
        const texture&     tex,
        const uv_rect&     local_UV,
        Params             params          = {1, 1, 0, 1, {0, 0}},
        bool chipped = false,
        unsigned char      alpha_threshold = 0)
        : repo_(repository)
        , tex_(tex)
        , params_(params)
        , local_UV_(local_UV)
    {
        local_transform_ = repo_.createTransform();
        repo_.getTransform(local_transform_).x = params_.origin.x;
        repo_.getTransform(local_transform_).y = params_.origin.y;

        if (chipped) {
            cols_ = tex_.width;
            rows_ = tex_.height;
            voxels_.resize(static_cast<size_t>(cols_ * rows_));
        } else {
            build_voxels(alpha_threshold);
        }
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
    //  Нанести урон в точке world_pos радиусом radius.
    //  Возвращает true если хотя бы один воксель уничтожен.
    // -----------------------------------------------------------------------
    bool apply_damage(const glm::vec2& world_pos, float damage, float radius = 0.f)
    {
        bool changed = false;
        const float cs = params_.cell_size;

        // Перевод мировых координат в индексы сетки с учётом центрирования
        int start_col = static_cast<int>((world_pos.x - radius - params_.origin.x + tex_.width  * cs * 0.5f) / cs);
        int end_col   = static_cast<int>((world_pos.x + radius - params_.origin.x + tex_.width  * cs * 0.5f) / cs) + 1;
        int start_row = static_cast<int>((world_pos.y - radius - params_.origin.y + tex_.height * cs * 0.5f) / cs);
        int end_row   = static_cast<int>((world_pos.y + radius - params_.origin.y + tex_.height * cs * 0.5f) / cs) + 1;

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
    //  Проверить связность сетки и отколоть несвязные фрагменты.
    //
    //  Алгоритм:
    //    1. BFS от первого живого вокселя — помечает «главный» компонент.
    //    2. Повторяет BFS для каждого непомеченного живого вокселя —
    //       каждый такой обход = отдельный фрагмент.
    //    3. Для каждого фрагмента создаётся новый DestructibleObject
    //       с теми же параметрами; его воксели получают спрайты,
    //       перенесённые из текущего объекта (без пересоздания).
    //    4. Фрагментные воксели в текущем объекте помечаются Absent,
    //       их spriteIdx обнуляется (спрайты переданы фрагменту).
    //
    //  Возвращает вектор отколовшихся фрагментов (пуст если объект связен).
    //  Текущий объект сохраняет наибольший компонент.
    // -----------------------------------------------------------------------
    std::vector<DestructibleObject> split_if_disconnected()
    {
        const int total = cols_ * rows_;
        std::vector<int> component(static_cast<size_t>(total), -1);  // -1 = не посещён
        int num_components = 0;

        // BFS по 4-связным соседям
        auto bfs = [&](int start_idx, int comp_id)
        {
            std::queue<int> q;
            q.push(start_idx);
            component[static_cast<size_t>(start_idx)] = comp_id;

            while (!q.empty())
            {
                const int idx = q.front(); q.pop();
                const int c = idx % cols_;
                const int r = idx / cols_;

                constexpr int dc[4] = {1, -1, 0, 0};
                constexpr int dr[4] = {0, 0, 1, -1};

                for (int d = 0; d < 4; ++d)
                {
                    const int nc = c + dc[d];
                    const int nr = r + dr[d];
                    if (nc < 0 || nc >= cols_ || nr < 0 || nr >= rows_) continue;

                    const int ni = nr * cols_ + nc;
                    if (component[static_cast<size_t>(ni)] != -1) continue;
                    if (voxels_[static_cast<size_t>(ni)].status == VoxelStatus::Absent) continue;

                    component[static_cast<size_t>(ni)] = comp_id;
                    q.push(ni);
                }
            }
        };

        // Обходим все живые воксели
        for (int i = 0; i < total; ++i)
        {
            if (voxels_[static_cast<size_t>(i)].status == VoxelStatus::Absent) continue;
            if (component[static_cast<size_t>(i)] != -1) continue;
            bfs(i, num_components++);
        }

        // Один компонент — объект связен, ничего не делаем
        if (num_components <= 1)
            return {};

        // Находим наибольший компонент — он остаётся в текущем объекте
        std::vector<int> comp_size(static_cast<size_t>(num_components), 0);
        for (int i = 0; i < total; ++i)
        {
            const int c = component[static_cast<size_t>(i)];
            if (c >= 0) ++comp_size[static_cast<size_t>(c)];
        }
        const int main_comp = static_cast<int>(
            std::max_element(comp_size.begin(), comp_size.end()) - comp_size.begin());

        // Создаём фрагменты для каждого второстепенного компонента
        std::vector<DestructibleObject> fragments;
        fragments.reserve(static_cast<size_t>(num_components - 1));

        for (int comp_id = 0; comp_id < num_components; ++comp_id)
        {
            if (comp_id == main_comp) continue;

            glm::vec4 frag_bounds = {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max()
            };

            for (int i = 0; i < total; ++i) {
                if (component[static_cast<size_t>(i)] != comp_id) continue;

                Voxel& src = voxels_[static_cast<size_t>(i)];

                frag_bounds[0] = std::min(frag_bounds[0], src.center.x);
                frag_bounds[1] = std::min(frag_bounds[1], src.center.y);
                frag_bounds[2] = std::max(frag_bounds[2], src.center.x);
                frag_bounds[3] = std::max(frag_bounds[3], src.center.y);
            }

            // Создаём пустой объект с теми же параметрами (build_voxels не вызывается)
            fragments.emplace_back(
                repo_,
                tex_,
                local_UV_,
                Params(
                    params_.cell_size,
                    params_.default_hp,
                    params_.depth,
                    params_.paralax,
                    {
                        (frag_bounds[0] + frag_bounds[2]) / 2,
                        (frag_bounds[1] + frag_bounds[3]) / 2
                    }
                ),
                true);
            DestructibleObject& frag = fragments.back();

            // Переносим воксели этого компонента
            for (int i = 0; i < total; ++i)
            {
                if (component[static_cast<size_t>(i)] != comp_id) continue;

                Voxel& src = voxels_[static_cast<size_t>(i)];
                Voxel& dst = frag.voxels_[static_cast<size_t>(i)];

                dst = src;           // копируем данные вокселя (center, hp, spriteIdx)
                repo_.dataFromSpriteIdx(dst.spriteIdx).transformIdx = frag.local_transform_;

                // Забираем спрайт из текущего объекта
                src.spriteIdx = ~size_t{0};
                src.status    = VoxelStatus::Absent;
            }

            glm::vec2 dOrign = params_.origin - frag.params_.origin;

            for (Voxel& voxel : frag.voxels_) {
                voxel.center.x += dOrign.x;
                voxel.center.y += dOrign.y;
            }

            frag.sync_sprites();

            // fragments.push_back(frag);
        }

        return fragments;
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

    void add_rotation(const float& dAngle)
    {
        repo_.getTransform(local_transform_).angle += dAngle;
    }
    void set_rotation(const float& angle)
    {
        repo_.getTransform(local_transform_).angle = angle;
    }

private:
    // -----------------------------------------------------------------------
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

                // Центрируем сетку вокруг origin
                vx.center = {
                    params_.origin.x + (col + 0.5f - cols_ * 0.5f) * cs,
                    params_.origin.y + (row + 0.5f - rows_ * 0.5f) * cs
                };
                vx.hp     = params_.default_hp;
                vx.status = VoxelStatus::Absent;

                // Проверяем альфа-канал (RGBA: 4 байта на пиксель, alpha = байт [3])
                if (tex_.colorData != nullptr)
                {
                    const size_t pixel_idx = static_cast<size_t>(row * cols_ + col);
                    if (tex_.colorData[pixel_idx * 4 + 3] <= alpha_threshold) continue;
                }

                vx.status = VoxelStatus::Active;

                SpriteData sd;
                sd.width      = cs;
                sd.height     = cs;
                sd.angle      = 0.f;
                sd.position_x = vx.center.x;
                sd.position_y = vx.center.y;
                sd.paralax    = params_.paralax;
                sd.uvRect.u0  = local_UV_.u0 + static_cast<float>(col) * pixel_u;
                sd.uvRect.v0  = local_UV_.v0 + static_cast<float>(row) * pixel_v;
                sd.uvRect.u1  = sd.uvRect.u0 + pixel_u;
                sd.uvRect.v1  = sd.uvRect.v0 + pixel_v;
                sd.transformIdx = local_transform_;

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

    void update_transform()
    {
        auto& transform = repo_.getTransform(local_transform_);
        transform.x = params_.origin.x;
        transform.y = params_.origin.y;
    }

    SpritesRepository& repo_;
    const texture&     tex_;
    Params             params_;
    uv_rect            local_UV_;
    size_t             local_transform_;

    std::vector<Voxel> voxels_;
    int cols_ = 0;
    int rows_ = 0;
};
