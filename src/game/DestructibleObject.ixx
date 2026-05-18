module;

#include "glm/glm.hpp"
#include "graphics/uv_rect.h"

#include <vector>
#include <algorithm>
#include <utility>
#include <queue>
#include <limits>
#include <cmath>

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
    Boundary = 2,  // граничный воксель
};

// ---------------------------------------------------------------------------
//  Воксель — один пиксель текстуры, отображённый как квадратный спрайт.
//  Позиция центрирована вокруг origin объекта: (col + 0.5 - cols/2) * cell_size.
//  Никаких случайных или граничных смещений.
// ---------------------------------------------------------------------------
export struct Voxel
{
    glm::vec2   center{};
    float       hp     = 1.f;
    VoxelStatus status = VoxelStatus::Absent;
    size_t      spriteIdx = ~size_t{0};

    [[nodiscard]] bool is_alive()   const { return status >= VoxelStatus::Active && hp > 0.f; }
    [[nodiscard]] bool has_sprite() const { return spriteIdx != ~size_t{0}; }
};

// ---------------------------------------------------------------------------
//  Точка контакта при коллизии двух объектов (мировые координаты)
// ---------------------------------------------------------------------------
export struct ContactPoint
{
    glm::vec2 world_pos;   // середина между двумя столкнувшимися вокселями
    glm::vec2 normal;      // нормаль от B к A (единичный вектор)
};

// ---------------------------------------------------------------------------
//  Вспомогательные функции преобразования координат
// ---------------------------------------------------------------------------
namespace collision_math
{
    // Поворот вектора на угол (радианы)
    inline glm::vec2 rotate_vec(glm::vec2 v, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return { v.x * c - v.y * s,
                 v.x * s + v.y * c };
    }

    // Локальное пространство объекта → мировое
    inline glm::vec2 local_to_world(glm::vec2 local, glm::vec2 pos, float angle)
    {
        return pos + rotate_vec(local, angle);
    }

    // Мировое → локальное пространство объекта
    inline glm::vec2 world_to_local(glm::vec2 world, glm::vec2 pos, float angle)
    {
        return rotate_vec(world - pos, -angle);
    }

    // Прямо из локального пространства A в локальное пространство B
    inline glm::vec2 a_local_to_b_local(glm::vec2 p,
                                         glm::vec2 posA, float angleA,
                                         glm::vec2 posB, float angleB)
    {
        return world_to_local(local_to_world(p, posA, angleA), posB, angleB);
    }
}

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

private:
    SpritesRepository& repo_;
    const texture&     tex_;
    Params             params_;
    uv_rect            local_UV_;
    size_t             local_transform_;
    size_t             parent_texture_;

    float mass_ = 1;
    float inertia_moment_ = 1;
    glm::vec2 orign_shift = {};

    std::vector<Voxel>   voxels_;
    std::vector<size_t>  boundary_;
    int cols_ = 0;
    int rows_ = 0;

public:
    glm::vec2 velocity = {0, 0};
    float angular_velocity = 0;

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
        const texture&     tex, const size_t texture_id,
        const uv_rect&     local_UV,
        Params             params          = {1, 1, 0, 1, {0, 0}},
        bool chipped = false,
        unsigned char      alpha_threshold = 0)
        : repo_(repository)
        , tex_(tex)
        , params_(params)
        , local_UV_(local_UV)
        , parent_texture_(texture_id)
    {
        local_transform_ = repo_.createTransform();
        repo_.getTransform(local_transform_).position = params_.origin;

        if (chipped) {
            cols_ = tex_.width;
            rows_ = tex_.height;
            voxels_.resize(static_cast<size_t>(cols_ * rows_));
        } else {
            build_voxels(alpha_threshold);
            recalculate_physic();
            build_local_chunks();
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
    [[nodiscard]] const std::vector<Voxel>& voxels() const { return voxels_; }
    [[nodiscard]] int grid_cols()                     const { return cols_; }
    [[nodiscard]] int grid_rows()                     const { return rows_; }

    [[nodiscard]] const Voxel* voxel_at(int col, int row) const
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
    //  После изменений пересчитывает чанки.
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

        if (changed)
            build_local_chunks();

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
                parent_texture_,
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
            frag.build_local_chunks();
        }

        // После передачи фрагментов пересчитываем чанки текущего объекта
        build_local_chunks();

        return fragments;
    }

    // -----------------------------------------------------------------------
    //  Сдвинуть позицию всех спрайтов (если не используется TransformNode)
    // -----------------------------------------------------------------------
    void sync_sprites()
    {
        for (auto& vx : voxels_)
        {
            if (!vx.has_sprite()) continue;
            SpriteData& sd = repo_.dataFromSpriteIdx(vx.spriteIdx);
            sd.position_x = vx.center.x;
            sd.position_y = vx.center.y;
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
    void set_global_rotation(const float& angle)
    {
        float global_angle = global_rotation();
        global_angle = std::fmod(global_angle, std::numbers::pi_v<float> * 2);
        add_rotation(angle - global_angle);
    }

    float get_angle() const
    {
        return repo_.getTransform(local_transform_).angle;
    }

    void add_position(const glm::vec2& pos)
    {
        repo_.getTransform(local_transform_).position += pos;
    }
    void set_position(const glm::vec2& pos)
    {
        repo_.getTransform(local_transform_).position = pos;
    }
    const glm::vec2& get_position() const
    {
        return repo_.getTransform(local_transform_).position;
    }

    const float& get_mass() const
    {
        return mass_;
    }

    const float& get_inertia_moment() const
    {
        return inertia_moment_;
    }

    const size_t& get_parent_texture() const
    {
        return parent_texture_;
    }

    // -----------------------------------------------------------------------
    //  Коллизия — Уровень 1: описывающая окружность
    //
    //  Не зависит от угла поворота — подходит как быстрый широкий тест.
    //  Радиус = половина диагонали AABB сетки вокселей.
    // -----------------------------------------------------------------------
    float get_bounding_radius() const
    {
        const float hw = cols_ * params_.cell_size * 0.5f;
        const float hh = rows_ * params_.cell_size * 0.5f;
        return std::sqrt(hw * hw + hh * hh);
    }

    bool bounding_circles_overlap(const DestructibleObject& other) const
    {
        const glm::vec2 d = get_position() - other.get_position();
        const float dist2 = d.x * d.x + d.y * d.y;
        const float rsum  = get_bounding_radius() + other.get_bounding_radius();
        return dist2 < rsum * rsum;
    }

    // -----------------------------------------------------------------------
    //  Коллизия — полная трёхуровневая проверка:
    //
    //    1. Описывающие окружности объектов (мировое пространство)
    //    2. Локальные чанки — через преобразование координат A.local → B.local
    //    3. Вписанные окружности вокселей (расстояние между центрами < cell_size)
    //
    //  Важно: чанки хранятся в локальном пространстве каждого объекта
    //  и не зависят от его позиции и угла — пересчёт нужен только при
    //  разрушении вокселей.
    //
    //  Возвращает список точек контакта в мировых координатах.
    //  Пустой вектор — коллизии нет.
    // -----------------------------------------------------------------------
    ContactPoint find_contacts(DestructibleObject& other)
    {
        std::vector<ContactPoint> contacts;
        ContactPoint final_contact;
        final_contact.normal = glm::vec2(0.0f);
        final_contact.world_pos = glm::vec2(0.0f);

        // Уровень 1 — описывающие окружности
        if (!bounding_circles_overlap(other))
            return final_contact;

        const glm::vec2 posA  = get_position();
        const glm::vec2 posB  = other.get_position();
        const float     angA  = get_angle();
        const float     angB  = other.get_angle();
        const float     cs    = params_.cell_size;
        // Два вокселя-квадрата со стороной cs перекрываются,
        // если расстояние между центрами меньше cs (вписанная окружность r = cs/2)
        const float touch_dist2 = cs * cs;

        auto squares_intersect = [](glm::vec2 center_a_local_b, float angle_a_rel_b,
                                glm::vec2 center_b, float half_size) -> bool
        {
            // center_b - центр вокселя B в локальной системе B (он же мировой для B, если бы не поворот, но мы в локальной)
            // В локальной системе B воксель B - это AABB [-hs, hs] вокруг center_b
            // Воксель A - это OBB вокруг center_a_local_b с углом angle_a_rel_b

            float hs = half_size; // cs / 2.0f
            glm::vec2 d = center_a_local_b - center_b;

            float cos_a = std::cos(angle_a_rel_b);
            float sin_a = std::sin(angle_a_rel_b);

            // SAT оси: оси вокселя A (так как он OBB) и оси вокселя B (так как он AABB)

            // 1. Ось X вокселя B (1, 0)
            // Проекция d на (1,0) = abs(d.x)
            // Проекция B на (1,0) = hs
            // Проекция A на (1,0) = hs * (|cos_a| + |sin_a|)
            if (std::abs(d.x) > hs + hs * (std::abs(cos_a) + std::abs(sin_a))) return false;

            // 2. Ось Y вокселя B (0, 1)
            // Проекция d на (0,1) = abs(d.y)
            // Проекция B на (0,1) = hs
            // Проекция A на (0,1) = hs * (|sin_a| + |cos_a|)
            if (std::abs(d.y) > hs + hs * (std::abs(sin_a) + std::abs(cos_a))) return false;

            // 3. Ось X вокселя A (cos_a, sin_a)
            float proj_d = std::abs(d.x * cos_a + d.y * sin_a);
            // Проекция B на эту ось: hs * (|cos_a| + |sin_a|)
            // Проекция A на эту ось: hs
            if (proj_d > hs + hs * (std::abs(cos_a) + std::abs(sin_a))) return false;

            // 4. Ось Y вокселя A (-sin_a, cos_a)
            float proj_d2 = std::abs(d.x * (-sin_a) + d.y * cos_a);
            // Проекция B на эту ось: hs * (|-sin_a| + |cos_a|) = hs * (|sin_a| + |cos_a|)
            // Проекция A на эту ось: hs
            if (proj_d2 > hs + hs * (std::abs(sin_a) + std::abs(cos_a))) return false;

            return true;
        };

        // Уровень 2 — перебираем пары чанков (в локальных пространствах)
        for (const LocalChunk& ca : local_chunks_)
        {
            // Центр чанка A переводим в локальное пространство B
            const glm::vec2 ca_center_B = collision_math::a_local_to_b_local(
                ca.local_center, posA, angA, posB, angB);

            for (const LocalChunk& cb : other.local_chunks_)
            {
                // Проверяем описывающие окружности чанков
                const glm::vec2 dc = ca_center_B - cb.local_center;
                const float dc2    = dc.x * dc.x + dc.y * dc.y;
                const float rsum   = ca.bounding_radius + cb.bounding_radius;

                if (dc2 > rsum * rsum) continue;

                // Уровень 3 — воксели внутри пересекающихся чанков
                for (int ra = ca.row_start; ra < ca.row_end; ++ra)
                {
                    for (int ca_col = ca.col_start; ca_col < ca.col_end; ++ca_col)
                    {
                        const Voxel* va = voxel_at(ca_col, ra);
                        if (!va || !va->is_alive()) continue;

                        // Центр вокселя A → локальное пространство B
                        const glm::vec2 va_in_B = collision_math::a_local_to_b_local(
                            va->center, posA, angA, posB, angB);

                        // Прямой поиск вокселя B по индексу (O(1))
                        const int col_b = static_cast<int>(
                            (va_in_B.x + other.cols_ * cs * 0.5f + other.orign_shift.x) / cs);
                        const int row_b = static_cast<int>(
                            (va_in_B.y + other.rows_ * cs * 0.5f + other.orign_shift.y) / cs);

                        // Проверяем и соседние клетки B на случай погрешности поворота
                        for (int dr = -1; dr <= 1; ++dr)
                        {
                            for (int dc_idx = -1; dc_idx <= 1; ++dc_idx)
                            {
                                const Voxel* vb = other.voxel_at(col_b + dc_idx, row_b + dr);
                                if (!vb || !vb->is_alive()) continue;

                                // Финальная проверка: пересечение квадратов
                                const float rel_angle = angA - angB;
                                const float half_cs = cs * 0.5f;

                                // va_in_B уже вычислен выше как центр A в локальной системе B
                                // vb->center - это центр B в локальной системе B

                                if (squares_intersect(va_in_B, rel_angle, vb->center, half_cs))
                                {

                                    const glm::vec2 va_world =
                                        collision_math::local_to_world(va->center, posA, angA);
                                    const glm::vec2 vb_world =
                                        collision_math::local_to_world(vb->center, posB, angB);

                                    // 1. Вычисляем скорости точек контакта в мировом пространстве
                                    // Линейная скорость + угловая скорость (перпендикулярный вектор радиуса)
                                    // Для 2D: cross(omega, r) -> (-omega * r.y, omega * r.x)

                                    glm::vec2 rA = va_world - posA;
                                    glm::vec2 vA = velocity + glm::vec2(-angular_velocity * rA.y, angular_velocity * rA.x);

                                    glm::vec2 rB = vb_world - posB;
                                    glm::vec2 vB = other.velocity + glm::vec2(-other.angular_velocity * rB.y, other.angular_velocity * rB.x);

                                    // 2. Относительная скорость A относительно B
                                    glm::vec2 vRel = vA - vB;

                                    // Нормаль: от B к A
                                    const glm::vec2 ab = vRel;
                                    const float ab_len = std::sqrt(ab.x * ab.x + ab.y * ab.y);
                                    const glm::vec2 normal = (ab_len > 1e-6f)
                                        ? ab
                                        : glm::vec2{1.f, 0.f};

                                    contacts.push_back({
                                        (va_world + vb_world) * 0.5f,
                                        normal
                                    });

                                }
                            }
                        }
                    }
                }
            }
        }

        if (!contacts.empty()) {
            for (auto& contact : contacts) {
                final_contact.normal += contact.normal;
                final_contact.world_pos += contact.world_pos;
            }
            final_contact.world_pos /= contacts.size();
            final_contact.normal /= contacts.size();
        }

        return final_contact;
    }

    // -----------------------------------------------------------------------
    //  Удобный метод: проверить коллизию и нанести урон в точках контакта.
    //  Возвращает true если коллизия произошла.
    // -----------------------------------------------------------------------

    void update_position(const float& dt)
    {
        add_rotation(angular_velocity * dt);
        add_position(velocity * dt);
    }

    int get_local_transform() const
    {
        return local_transform_;
    }

    void set_parent_transform(int index)
    {
        repo_.getTransform(local_transform_).parentIndex = index;
    }

    int get_parent_transform()
    {
        return repo_.getTransform(local_transform_).parentIndex;
    }

    glm::vec2 global_position()
    {
        return repo_.getGlobalPosition(local_transform_);
    }

    float global_rotation()
    {
        return repo_.getGlobalRotation(local_transform_);
    }

    float local_rotation()
    {
        return repo_.getTransform(local_transform_).angle;
    }

private:
    // -----------------------------------------------------------------------
    //  Локальный чанк (блок вокселей в локальном пространстве объекта).
    //  Хранит AABB и описывающую окружность — не зависит от поворота объекта.
    // -----------------------------------------------------------------------
    struct LocalChunk
    {
        glm::vec2 local_min;       // AABB в локальных координатах
        glm::vec2 local_max;
        glm::vec2 local_center;    // центр AABB
        float     bounding_radius; // радиус описывающей окружности чанка

        int col_start, row_start;
        int col_end,   row_end;
    };

    static constexpr int CHUNK_SIZE = 10;
    std::vector<LocalChunk> local_chunks_;

    // -----------------------------------------------------------------------
    //  Построить/перестроить локальные чанки.
    //  Вызывается после build_voxels, apply_damage и split_if_disconnected.
    // -----------------------------------------------------------------------
    void build_local_chunks()
    {
        local_chunks_.clear();
        const float cs = params_.cell_size;

        for (int cr = 0; cr * CHUNK_SIZE < rows_; ++cr)
        {
            for (int cc = 0; cc * CHUNK_SIZE < cols_; ++cc)
            {
                LocalChunk chunk;
                chunk.col_start = cc * CHUNK_SIZE;
                chunk.row_start = cr * CHUNK_SIZE;
                chunk.col_end   = std::min(chunk.col_start + CHUNK_SIZE, cols_);
                chunk.row_end   = std::min(chunk.row_start + CHUNK_SIZE, rows_);

                // AABB в локальных координатах (без позиции и угла объекта)
                chunk.local_min = voxels_[chunk.col_start + chunk.row_start * cols_].center - cs * 0.5f;
                chunk.local_max = voxels_[(chunk.col_end - 1) + (chunk.row_end - 1) * cols_].center + cs * 0.5f;
                chunk.local_center  = (chunk.local_min + chunk.local_max) * 0.5f;

#ifdef CHUNK_DEBUG
                repo_.addSprite({
                    chunk.local_max.x - chunk.local_min.x,
                    chunk.local_max.y - chunk.local_min.y,
                    0,
                    chunk.local_center.x,
                    chunk.local_center.y,
                    1,
                    -0.5,
                    {0, 0, 1, 1},
                    static_cast<unsigned int>(local_transform_)
                }, -0.5, local_transform_);
#endif

                const glm::vec2 half = (chunk.local_max - chunk.local_min) * 0.5f;
                chunk.bounding_radius = std::sqrt(half.x * half.x + half.y * half.y);

                // Пропускаем чанки без живых вокселей
                bool has_active = false;
                for (int r = chunk.row_start; r < chunk.row_end && !has_active; ++r)
                    for (int c = chunk.col_start; c < chunk.col_end && !has_active; ++c)
                        if (auto* vx = voxel_at(c, r); vx && vx->is_alive())
                            has_active = true;

                if (has_active)
                    local_chunks_.push_back(chunk);
            }
        }
    }

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
                    (col + 0.5f - cols_ * 0.5f) * cs,
                    (row + 0.5f - rows_ * 0.5f) * cs
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
                sd.depth      = params_.depth;
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

    void recalculate_physic()
    {
        orign_shift = {0.f, 0.f};
        float alive_num = 0.f;

        for (auto& voxel : voxels_) {
            if (!voxel.is_alive()) continue;
            orign_shift += voxel.center;
            alive_num += 1.f;
        }

        if (alive_num < 1.f) return;

        orign_shift /= alive_num;
        mass_ = alive_num;

        // Сдвигаем локальные центры вокселей в систему центра масс
        for (auto& voxel : voxels_) {
            voxel.center -= orign_shift;  // ← был += (неверный знак) и закомментировано

            if (voxel.is_alive()) {
                auto* data_ptr = repo_.getDataPointer(voxel.spriteIdx);
                data_ptr->position_x -= orign_shift.x;
                data_ptr->position_y -= orign_shift.y;
            }
        }

        // Смещаем мировую позицию трансформа на ту же величину
        params_.origin += orign_shift;

        // Момент инерции считается по уже обновлённым voxel.center
        inertia_moment_ = 0.f;
        constexpr float self_inertia = 1.f / 12.f * 2.f; // cs=1 → cs²/6
        for (auto& voxel : voxels_) {
            if (!voxel.is_alive()) continue;
            inertia_moment_ += glm::dot(voxel.center, voxel.center) + self_inertia;
        }

        update_transform();
        build_local_chunks();
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

    void update_boundary()
    {
        for (int i = 0; i < (cols_ * rows_); ++i) {
            if (i < cols_ || // верхняя граница
                (i + 1) % (rows_ + 1) == 0 || // правая граница
                i % (rows_ + 1) == 0 || // левая граница
                i < cols_ * (rows_ - 1)) // нижняя граница
            {
                if (voxels_[i].is_alive()) {
                    voxels_[i].status = VoxelStatus::Boundary;
                }
                continue;
            }

            if (voxels_[i].is_alive()) {
                if (voxels_[i + 1].is_alive() &&
                    voxels_[i - 1].is_alive() &&
                    voxels_[i + cols_].is_alive() &&
                    voxels_[i - cols_].is_alive()
                ) voxels_[i].status = VoxelStatus::Active;
                else {
                    voxels_[i].status = VoxelStatus::Boundary;
                }
            }
        }
    }

    std::vector<Voxel> get_boundary()
    {
        std::vector<Voxel> boundary_voxels;
        for (const size_t& i : boundary_) {
            boundary_voxels.push_back(voxels_[i]);
        }
        return boundary_voxels;
    }

    void update_transform()
    {
        auto& transform = repo_.getTransform(local_transform_);
        transform.position = params_.origin;
    }
};
