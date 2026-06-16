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
import additional_math;
import transform_node;

// ---------------------------------------------------------------------------
//  Статусы вокселей
// ---------------------------------------------------------------------------
export enum class VoxelStatus : std::uint8_t
{
    Absent   = 0,  // пустая ячейка
    Active   = 1,  // живой воксель
    Boundary = 2,  // граничный воксель
    Destroyed = 3,
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
// private:
//     struct FragmentData()
//     {
//
//     }
//
public:
    static constexpr voxel_object_params standart_params = voxel_object_params(5.f, 1.f, 0.f, 1.f, {0.f, 0.f}, 0.f);

private:
    voxel_object_params params_;
    uv_rect local_UV_;
    size_t grid_center_transform_;
    size_t mass_center_transform_;
    size_t parent_texture_;
    std::unique_ptr<RecursiveHitbox> hitbox_;

    float mass_ = 1;
    float inertia_moment_ = 1;

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
    size_t onSceneId_;

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
        grid_center_transform_ = repo_.createTransform();
        mass_center_transform_ = repo_.createTransform();
        TransformNode& mass_transform = repo_.getTransform(mass_center_transform_);
        TransformNode& grid_transform = repo_.getTransform(grid_center_transform_);
        grid_transform.parentIndex = mass_center_transform_;
        mass_transform.position = params_.origin;
        mass_transform.angle = params_.angle;

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

        struct DestroyedVoxelData {
            glm::vec2 world_pos;
        };
        std::vector<DestroyedVoxelData> destroyed_voxels;
        destroyed_voxels.reserve(100);

        const float& cs = params_.cell_size;
        const float& radius = explosion.radius;
        const float& angle = global_rotation();

        float cos_value = cos(angle);
        float sin_value = sin(angle);
        glm::vec2 grid_center = global_position() + math::rotate_vec2(repo_.getTransform(grid_center_transform_).position, angle);

        glm::vec2 diff = position - grid_center;
        glm::vec2 local_position = glm::vec2(
            diff.x * cos_value - diff.y * -sin_value,
            diff.x * -sin_value + diff.y * cos_value);

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
                if (!vx.is_alive()) continue;

                if (radius > 0.f)
                {
                    glm::vec2 voxel_diff = vx.center - local_position;
                    if (voxel_diff.x * voxel_diff.x + voxel_diff.y * voxel_diff.y > radius * radius) continue;
                }

                vx.hp -= explosion.strength;
                if (vx.hp <= 0.f)
                {
                    vx.hp = 0.f;
                    destroyed_voxels.push_back({grid_center + math::rotate_vec2(vx.center, angle)});
                    destroy_voxel(vx);
                    changed = true;
                }
            }
        }

        if (changed) {
            std::vector<std::unique_ptr<events::SceneEvent>> events;
            if (recalculate_physic()) {
                std::vector<std::unique_ptr<VoxelObject>> fragments = split_if_disconnected();

                if (!destroyed_voxels.empty()) {
                    // Собираем все возможные цели: основной объект + все фрагменты
                    struct Target {
                        VoxelObject* obj;
                        glm::vec2 cm;
                    };
                    std::vector<Target> targets;
                    targets.reserve(fragments.size() + 1);

                    targets.push_back({this, get_position()});
                    for (auto& frag : fragments) {
                        targets.push_back({frag.get(), frag->get_position()});
                    }

                    for (const auto& dv : destroyed_voxels) {
                        // 1. Направление от центра взрыва к уничтоженному вокселю
                        glm::vec2 dir_voxel = dv.world_pos - position;
                        float len_voxel = glm::length(dir_voxel);
                        if (len_voxel < 0.001f) continue; // Взрыв прямо в вокселе, игнорируем, чтобы не делить на 0
                        dir_voxel /= len_voxel;

                        // 2. Ищем цель с наименьшим углом (максимальный dot product)
                        float max_dot = -2.0f; // cos(180°) = -1, так что -2.0f гарантированно меньше любого реального значения
                        VoxelObject* best_target = nullptr;

                        for (const auto& t : targets) {
                            glm::vec2 dir_cm = t.cm - position;
                            float len_cm = glm::length(dir_cm);

                            if (len_cm < 0.001f) {
                                // Взрыв произошёл точно в центре масс этого объекта.
                                // Это идеальное совпадение (угол 0, dot = 1).
                                max_dot = 2.0f;
                                best_target = t.obj;
                                break;
                            }

                            dir_cm /= len_cm;

                            // Скалярное произведение = cos(угла). Чем больше, тем меньше угол.
                            float dot = glm::dot(dir_voxel, dir_cm);
                            if (dot > max_dot) {
                                max_dot = dot;
                                best_target = t.obj;
                            }
                        }

                        // 3. Применяем импульс только к победителю
                        if (best_target) {
                            // Магнитуда импульса.
                            // Если хочешь зависимость от расстояния, раскомментируй нужную строку:
                            // float magnitude = explosion.impulse; // Константная сила
                            // float magnitude = explosion.impulse * len_voxel; // Чем дальше, тем сильнее (как ты просил ранее)
                            float magnitude = explosion.impulse / (len_voxel * len_voxel); // Классическое затухание (рекомендую)

                            glm::vec2 impulse_vec = dir_voxel * magnitude;
                            best_target->apply_impulse_at(impulse_vec, dv.world_pos);
                        }
                    }
                }

                for (std::unique_ptr<VoxelObject>& fragmentPtr : fragments) {
                    events.emplace_back(std::make_unique<events::AddObject>(std::move(fragmentPtr)));
                }
            }
            else {
                events.emplace_back(std::make_unique<events::SelfDestruct>());
            }
            newEventFunc_(invokeFunctionContext_, events);
        }
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

            glm::vec2 frag_center = global_position() + math::rotate_vec2(repo_.getTransform(grid_center_transform_).position, global_rotation());
            const float& global_angle = get_angle();

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
                    frag_center,
                    global_angle
                ),
                true)));
            VoxelObject& frag = *fragments.back();
            //frag.recalculate_physic();

            // Переносим воксели этого компонента
            for (int i = 0; i < total; ++i)
            {
                if (component[static_cast<size_t>(i)] != comp_id) continue;

                Voxel& src = voxels_[static_cast<size_t>(i)];
                Voxel& dst = frag.voxels_[static_cast<size_t>(i)];

                dst = src;           // копируем данные вокселя (center, hp, spriteIdx)
                repo_.dataFromSpriteIdx(dst.spriteIdx).transformIdx = frag.grid_center_transform_;

                // Забираем спрайт из текущего объекта
                src.spriteIdx = ~size_t{0};
                src.status    = VoxelStatus::Absent;
            }

            frag.recalculate_physic();
            frag.sync_sprites();

            {
                const glm::vec2 r_vec = frag.get_position() - get_position();

                const float omega = angular_velocity_;
                const glm::vec2 v_add(-omega * r_vec.y, omega * r_vec.x);

                frag.set_velocity(velocity_ + v_add);
                frag.set_angular_velocity(omega);
            }
        }

        // После передачи фрагментов пересчитываем чанки текущего объекта
        recalculate_physic();

        return fragments;
    }

    void onConnectInit(void* context, EventInvokingFunction* func, const size_t& id) override
    {
        invokeFunctionContext_ = context;
        newEventFunc_ = func;
        onSceneId_ = id;
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
        return repo_.getTransform(mass_center_transform_).angle;
    }
    void add_angle(const float& dAngle) override
    {
        repo_.getTransform(mass_center_transform_).angle += dAngle;
    }
    void set_angle(const float& angle) override
    {
        repo_.getTransform(mass_center_transform_).angle = angle;
    }

    void set_global_angle(const float& angle)
    {
        float global_angle = global_rotation();
        global_angle = std::fmod(global_angle, std::numbers::pi_v<float> * 2);
        add_angle(angle - global_angle);
    }

    void add_position(const glm::vec2& pos) override
    {
        repo_.getTransform(mass_center_transform_).position += pos;
    }
    void set_position(const glm::vec2& pos) override
    {
        repo_.getTransform(mass_center_transform_).position = pos;
    }
    const glm::vec2& get_position() const override
    {
        return repo_.getTransform(mass_center_transform_).position;
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
                RecursiveHitbox::CollisionCheckerInput(
                    other.get_hitbox(),
                    other.get_position(),
                    other.get_angle())
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
#ifndef DEBUG_COLLISION
    void update(const float& dt) override
#else
    void update(const float& dt, bool debug_draw) override
#endif
    {
        add_angle(angular_velocity_ * dt);
        add_position(velocity_ * dt);

#ifdef DEBUG_COLLISION
        if (debug_draw) {
            DebugDraw();
        }
#endif
    }

    int get_grid_transform() const
    {
        return grid_center_transform_;
    }

    int get_mass_transform() const
    {
        return mass_center_transform_;
    }

    void set_parent_transform(int index)
    {
        repo_.getTransform(mass_center_transform_).parentIndex = index;
    }

    int get_parent_transform()
    {
        return repo_.getTransform(grid_center_transform_).parentIndex;
    }

    glm::vec2 global_position()
    {
        return repo_.getGlobalPosition(mass_center_transform_);
    }

    float global_rotation()
    {
        return repo_.getGlobalRotation(mass_center_transform_);
    }

    float local_rotation()
    {
        return repo_.getTransform(mass_center_transform_).angle;
    }

    const size_t& id() override
    {
        return onSceneId_;
    }

#ifdef DEBUG_COLLISION
    void DebugDraw() const override
    {
        hitbox_->debugDraw(get_position(), get_angle());
    }
#endif

private:
    void build_local_chunks()
    {
        GridBoundary bound = {
            std::numeric_limits<size_t>::max(),
            std::numeric_limits<size_t>::max(),
            std::numeric_limits<size_t>::min(),
            std::numeric_limits<size_t>::min()
        };

        for (size_t x = 0; x < cols_; x++)
        for (size_t y = 0; y < rows_; y++) {
            if (voxel_at(x, y)->is_alive()) {
                bound.x1 = std::min(bound.x1, x);
                bound.y1 = std::min(bound.y1, y);
                bound.x2 = std::max(bound.x2, x + 1);
                bound.y2 = std::max(bound.y2, y + 1);
            }
        }

        glm::vec2 size = {
            (bound.x2 - bound.x1) * params_.cell_size,
            (bound.y2 - bound.y1) * params_.cell_size
            };

        glm::vec2 hitbox_shift = {
             (bound.x1 * params_.cell_size + size.x * 0.5f) - cols_ * 0.5f * params_.cell_size,
             (bound.y1 * params_.cell_size + size.y * 0.5f) - rows_ * 0.5f * params_.cell_size
            };
        hitbox_shift += repo_.getTransform(grid_center_transform_).position;

        hitbox_ = std::make_unique<RecursiveHitbox>(
        bound,
        3,
        size,
        hitbox_shift,
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
        // int up = std::numeric_limits<int>::max();
        // int down = std::numeric_limits<int>::min();
        // int left = std::numeric_limits<int>::max();
        // int right = std::numeric_limits<int>::min();
        //
        // for (int x = 0; x < tex_.width; x++) {
        //     for (int y = 0; y < tex_.height; y++) {
        //
        //         const size_t pixel_idx = static_cast<size_t>(y * tex_.width + x);
        //         if (tex_.colorData[pixel_idx * 4 + 3] <= alpha_threshold) continue;
        //
        //         up = std::min(up, y);
        //         down = std::max(down, y);
        //         left = std::min(left, x);
        //         right = std::max(right, x);
        //     }
        // }

        cols_ = tex_.width;
        rows_ = tex_.height;

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
                    const size_t pixel_idx = static_cast<size_t>(row * tex_.width + col);
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
                sd.uvRect.u0  = local_UV_.u0 + static_cast<float>(col) * pixel_u;
                sd.uvRect.v0  = local_UV_.v0 + static_cast<float>(row) * pixel_v;
                sd.uvRect.u1  = sd.uvRect.u0 + pixel_u;
                sd.uvRect.v1  = sd.uvRect.v0 + pixel_v;
                sd.color      = {255, 255, 255, 255};
                sd.transformIdx = grid_center_transform_;

                vx.spriteIdx = repo_.addSprite(sd);
                repo_[vx.spriteIdx].textureDesc = tex_desc;
            }
        }
    }

    bool recalculate_physic()
    {
        glm::vec2 new_mass_center = {0.f, 0.f};
        float alive_num = 0.f;

        for (auto& voxel : voxels_) {
            if (!voxel.is_alive()) continue;
            new_mass_center += voxel.center;
            alive_num += 1.f;
        }

        if (alive_num < 1.f) return false;

        new_mass_center /= alive_num;
        mass_ = alive_num;

        TransformNode& mass_center = repo_.getTransform(mass_center_transform_);
        TransformNode& grid_center = repo_.getTransform(grid_center_transform_);

        glm::vec2 mass_center_shift = new_mass_center + grid_center.position;

        mass_center.position += math::rotate_vec2(mass_center_shift, get_angle());
        grid_center.position = -new_mass_center;

        inertia_moment_ = 0.f;
        constexpr float self_inertia = 1.f / 12.f * 2.f;
        for (auto& voxel : voxels_) {
            if (!voxel.is_alive()) continue;
            glm::vec2 voxel_center = voxel.center + new_mass_center;
            inertia_moment_ += glm::dot(voxel_center, voxel_center) + self_inertia;
        }

        // mass_center_ = new_mass_center;
        build_local_chunks();
        return true;
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

    void destroy_voxel(Voxel& vx)
    {
        if (vx.has_sprite())
        {
            repo_.removeSprite(vx.spriteIdx);
            vx.spriteIdx = ~size_t{0};
        }
        vx.status = VoxelStatus::Absent;
    }
};
