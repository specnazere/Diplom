module;

#include "glm/glm.hpp"

#include "graphics/uv_rect.h"
#include "explosion_data.h"
#include "voxel_object_params.h"

#include <vector>
#include <algorithm>
#include <utility>
#include <queue>
#include <limits>
#include <cmath>
#include <memory>

export module voxel_object;

import sprite;
import sprite_repository;
import texture;
import atlas;
import recursive_hitbox;
import scene_object;

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

export
class VoxelObject : public SceneObject
{
public:
    static constexpr voxel_object_params standart_params = voxel_object_params(5.f, 1.f, 0.f, 1.f, {0.f, 0.f});

private:
    voxel_object_params params_;
    uv_rect local_UV_;
    size_t local_transform_;
    size_t parent_texture_;
    std::unique_ptr<RecursiveHitbox> hitbox_;

    float mass_ = 1;
    float inertia_moment_ = 1;
    glm::vec2 orign_shift_ = {};

    std::vector<Voxel>   voxels_;
    std::vector<size_t>  boundary_;
    int cols_ = 0;
    int rows_ = 0;

    glm::vec2 velocity_ = {0, 0};
    float angular_velocity_ = 0;

protected:
    SpritesRepository& repo_;
    const texture& tex_;

    EventInvokingFunction* newEventFunc_;
    void* invokeFunctionContext_;

public:
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
    VoxelObject(
        SpritesRepository& repository,
        const texture&     tex,
        const size_t texture_id,
        const uv_rect&     local_UV,
        const voxel_object_params& params = standart_params,
        bool chipped = false,
        unsigned char alpha_threshold = 0)
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
        }
    }

    VoxelObject(const VoxelObject&)            = delete;
    VoxelObject& operator=(const VoxelObject&) = delete;
    VoxelObject(VoxelObject&&)                 = default;
    VoxelObject& operator=(VoxelObject&&)      = default;

    ~VoxelObject()
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
    void area_damage(const explosion_data& explosion, const glm::vec2& position) override
    {
        bool changed = false;

        const float& cs = params_.cell_size;
        const float& radius = explosion.radius;
        const float& grid_angle = global_rotation();

        float cos_value = cos(grid_angle);
        float sin_value = sin(grid_angle);
        glm::vec2 grid_center = global_position() - glm::vec2(
            params_.origin.x * cos_value - params_.origin.y * sin_value,
            params_.origin.x * sin_value + params_.origin.y * cos_value);

        glm::vec2 diff = position - grid_center;

        glm::vec2 local_position = glm::vec2(
            diff.x * cos_value - diff.y * sin_value,
            diff.x * sin_value + diff.y * cos_value);

        // Перевод мировых координат в индексы сетки с учётом центрирования
        int start_col = static_cast<int>((local_position.x - radius + (cols_ * cs) * 0.5f) / cs);
        int end_col   = static_cast<int>((local_position.x + radius + (cols_ * cs) * 0.5f) / cs) + 1;
        int start_row = static_cast<int>((local_position.y - radius + (rows_ * cs) * 0.5f) / cs);
        int end_row   = static_cast<int>((local_position.y + radius + (rows_ * cs) * 0.5f) / cs) + 1;

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
                    glm::vec2 voxel_diff = vx.center - local_position;
                    if (voxel_diff.x * voxel_diff.x + voxel_diff.y * voxel_diff.y > radius * radius) continue;
                }

                vx.hp -= explosion.strength;
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
    std::vector<std::unique_ptr<VoxelObject>> split_if_disconnected()
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
        std::vector<std::unique_ptr<VoxelObject>> fragments;
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
            fragments.push_back(std::move(std::make_unique<VoxelObject>(
                repo_,
                tex_,
                parent_texture_,
                local_UV_,
                voxel_object_params(
                    params_.cell_size,
                    params_.default_hp,
                    params_.depth,
                    params_.paralax,
                    {
                        (frag_bounds[0] + frag_bounds[2]) * 0.5f,
                        (frag_bounds[1] + frag_bounds[3]) * 0.5f
                    }
                ),
                true)));
            VoxelObject& frag = *fragments.back();

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

    void setupEventInvokingFunction(void* context, EventInvokingFunction* func) override
    {
        invokeFunctionContext_ = context;
        newEventFunc_ = func;
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
            sd.position = vx.center;
        }
    }

    const float& get_angle() const override
    {
        return repo_.getTransform(local_transform_).angle;
    }
    void add_angle(const float& dAngle) override
    {
        repo_.getTransform(local_transform_).angle += dAngle;
    }
    void set_angle(const float& angle) override
    {
        repo_.getTransform(local_transform_).angle = angle;
    }

    void set_global_angle(const float& angle)
    {
        float global_angle = global_rotation();
        global_angle = std::fmod(global_angle, std::numbers::pi_v<float> * 2);
        add_angle(angle - global_angle);
    }

    void add_position(const glm::vec2& pos) override
    {
        repo_.getTransform(local_transform_).position += pos;
    }
    void set_position(const glm::vec2& pos) override
    {
        repo_.getTransform(local_transform_).position = pos;
    }
    const glm::vec2& get_position() const override
    {
        return repo_.getTransform(local_transform_).position;
    }

    const float& get_angular_velocity() const override
    {
        return angular_velocity_;
    }
    void add_angular_velocity(const float& dVelocity) override
    {
        angular_velocity_ += dVelocity;
    }
    void set_angular_velocity(const float& velocity) override
    {
        angular_velocity_ = velocity;
    }

    const glm::vec2& get_velocity() const override
    {
        return velocity_;
    }
    void add_velocity(const glm::vec2& velocity) override
    {
        velocity_ += velocity;
    }
    void set_velocity(const glm::vec2& velocity) override
    {
        velocity_ = velocity;
    }

    const float& get_mass() const override
    {
        return mass_;
    }
    const float& get_moment() const override
    {
        return inertia_moment_;
    }

    const size_t& get_parent_texture() const override
    {
        return parent_texture_;
    }

    const RecursiveHitbox& get_hitbox() const override
    {
        return *hitbox_;
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

    bool bounding_circles_overlap(const VoxelObject& other) const
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
    std::optional<glm::vec2> find_contact(const PhysicObject& other) const override
    {
        return RecursiveHitbox::checkCollision(
                RecursiveHitbox::CollisionCheckerInput(*hitbox_, get_position(), get_angle()),
                RecursiveHitbox::CollisionCheckerInput(other.get_hitbox(), other.get_position(), other.get_angle())
            );
    }

    // Применяет мгновенный импульс в точке world_pos
    // impulse — вектор импульса (кг*м/с)
    void apply_impulse_at(const glm::vec2& impulse, const glm::vec2& world_pos) override {
        // 1. Изменение линейной скорости: v += J / m
        velocity_ += impulse / mass_;

        // 2. Изменение угловой скорости: w += (r x J) / I
        glm::vec2 r = world_pos - get_position(); // плечо силы
        float torque = r.x * impulse.y - r.y * impulse.x; // 2D cross product
        angular_velocity_ += torque / inertia_moment_;
    }

    // Опционально: метод для "статического" удара (как ты просил для пуль)
    // Просто меняет скорость, игнорируя массу другого тела (если нужно)
    void apply_static_impulse(const glm::vec2& impulse) {
        velocity_ += impulse / mass_;
        // Угловую можно не менять, если удар в центр масс, или рассчитать отдельно
    }

    void invokeEventsFromThis(std::vector<std::unique_ptr<events::SceneEvent>> outside_events) override
    {
        newEventFunc_(invokeFunctionContext_, outside_events);
    }

    // -----------------------------------------------------------------------
    //  Удобный метод: проверить коллизию и нанести урон в точках контакта.
    //  Возвращает true если коллизия произошла.
    // -----------------------------------------------------------------------

    void update(const float& dt) override
    {
        add_angle(angular_velocity_ * dt);
        add_position(velocity_ * dt);
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

#ifdef DEBUG_COLLISION
    void DebugDraw() const
    {
        hitbox_->debugDraw(get_position(), get_angle());
    }
#endif

private:
    void build_local_chunks()
    {
        hitbox_ = std::make_unique<RecursiveHitbox>(
        GridBoundary(0, 0, static_cast<size_t>(cols_), static_cast<size_t>(rows_)),
        3,
        glm::vec2(cols_ * params_.cell_size, rows_ * params_.cell_size),
        -orign_shift_,
        [this](const GridBoundary& boundary)
            {
                for (int x = boundary.x1; x < boundary.x2; x++) {
                    for (int y = boundary.y1; y < boundary.y2; y++) {
                        if (voxel_at(x, y)->is_alive()) {
                            return true;
                        }
                    }
                }
                return false;
            }
        );
    }

    void build_voxels(unsigned char alpha_threshold)
    {
        int up = std::numeric_limits<int>::max();
        int down = std::numeric_limits<int>::min();
        int left = std::numeric_limits<int>::max();
        int right = std::numeric_limits<int>::min();

        for (int x = 0; x < tex_.width; x++) {
            for (int y = 0; y < tex_.height; y++) {

                const size_t pixel_idx = static_cast<size_t>(y * tex_.width + x);
                if (tex_.colorData[pixel_idx * 4 + 3] <= alpha_threshold) continue;

                up = std::min(up, y);
                down = std::max(down, y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }

        cols_ = right - left + 1;
        rows_ = down - up + 1;

        if (cols_ <= 0 || rows_ <= 0) return;

        voxels_.resize(static_cast<size_t>(cols_ * rows_));

        const float cs      = params_.cell_size;
        const float pixel_u = (local_UV_.u1 - local_UV_.u0) / static_cast<float>(tex_.width);
        const float pixel_v = (local_UV_.v1 - local_UV_.v0) / static_cast<float>(tex_.height);
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
                    const size_t pixel_idx = static_cast<size_t>((row + up) * tex_.width + (col + left));
                    if (tex_.colorData[pixel_idx * 4 + 3] <= alpha_threshold) continue;
                }

                vx.status = VoxelStatus::Active;

                SpriteData sd;
                sd.width      = cs;
                sd.height     = cs;
                sd.angle      = 0.f;
                sd.depth      = params_.depth;
                sd.position   = vx.center;
                sd.paralax    = params_.paralax;
                sd.uvRect.u0  = local_UV_.u0 + static_cast<float>(col + left) * pixel_u;
                sd.uvRect.v0  = local_UV_.v0 + static_cast<float>(row + up) * pixel_v;
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
        orign_shift_ = {0.f, 0.f};
        float alive_num = 0.f;

        for (auto& voxel : voxels_) {
            if (!voxel.is_alive()) continue;
            orign_shift_ += voxel.center;
            alive_num += 1.f;
        }

        if (alive_num < 1.f) return;

        orign_shift_ /= alive_num;
        mass_ = alive_num;

        // Сдвигаем локальные центры вокселей в систему центра масс
        for (auto& voxel : voxels_) {
            voxel.center -= orign_shift_;  // ← был += (неверный знак) и закомментировано

            if (voxel.is_alive()) {
                auto* data_ptr = repo_.getDataPointer(voxel.spriteIdx);
                data_ptr->position -= orign_shift_;
            }
        }

        // Смещаем мировую позицию трансформа на ту же величину
        params_.origin = orign_shift_;

        // Момент инерции считается по уже обновлённым voxel.center
        inertia_moment_ = 0.f;
        constexpr float self_inertia = 1.f / 12.f * 2.f;
        for (auto& voxel : voxels_) {
            if (!voxel.is_alive()) continue;
            inertia_moment_ += glm::dot(voxel.center, voxel.center) + self_inertia;
        }

        for (Voxel& voxel : voxels_) {
            voxel.center += orign_shift_;
        }

        add_position(orign_shift_);
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
