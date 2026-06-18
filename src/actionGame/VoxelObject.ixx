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

export import sprite;
export import sprite_repository;
export import texture;
export import atlas;
export import recursive_hitbox;
export import scene_object;
export import additional_math;
export import transform_node;
export import voxel_health_protocol;
export import native_tcp;

export enum class VoxelStatus : std::uint8_t
{
    Absent   = 0,  // пустая ячейка
    Destroyed,
    Solid,
    Boundary,
};

export struct Voxel
{
    glm::vec2   center{};
    float       hp     = 1.f;
    VoxelStatus status = VoxelStatus::Absent;
    size_t      spriteIdx = ~size_t{0};

    [[nodiscard]] bool is_alive()   const { return status >= VoxelStatus::Solid && hp > 0.f; }
    [[nodiscard]] bool has_sprite() const { return spriteIdx != ~size_t{0}; }
};

export
class VoxelObject : public SceneObject
{
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
    const texture& tex_;

    EventInvokingFunction* newEventFunc_ = nullptr;
    void* invokeFunctionContext_ = nullptr;
    size_t onSceneId_{};

public:
    VoxelObject(
        const texture& tex,
        size_t texture_id,
        uv_rect local_UV,
        const voxel_object_params& params = standart_params,
        bool chipped = false,
        unsigned char alpha_threshold = 0)
        : tex_(tex)
        , params_(params)
        , local_UV_(local_UV)
        , parent_texture_(texture_id)
    {
        auto& repo = SpritesRepository::get_active();
        grid_center_transform_ = repo.create_transform();
        mass_center_transform_ = repo.create_transform();
        TransformNode& mass_transform = repo.get_transform(mass_center_transform_);
        TransformNode& grid_transform = repo.get_transform(grid_center_transform_);
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
    VoxelObject(VoxelObject&&)                 = delete;
    VoxelObject& operator=(VoxelObject&&)      = delete;

    ~VoxelObject() override
    {
        auto& repo = SpritesRepository::get_active();
        for (auto& vx : voxels_)
            if (vx.has_sprite())
                repo.free_sprite(vx.spriteIdx);
    }

    [[nodiscard]] const std::vector<Voxel>& voxels() const { return voxels_; }
    [[nodiscard]] int grid_cols()                     const { return cols_; }
    [[nodiscard]] int grid_rows()                     const { return rows_; }

    [[nodiscard]] inline const Voxel* voxel_at(const int& col, const int& row) const
    {
        if (col < 0 || col >= cols_ || row < 0 || row >= rows_) return nullptr;
        return &voxels_[static_cast<size_t>(row * cols_ + col)];
    }
    [[nodiscard]] inline Voxel* voxel_at(const int& col, const int& row)
    {
        return const_cast<Voxel*>(std::as_const(*this).voxel_at(col, row));
    }

    void area_damage(explosion_data explosion, glm::vec2 position) override
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
        auto& repo = SpritesRepository::get_active();
        glm::vec2 grid_center = global_position() + math::rotate_vec2(repo.get_transform(grid_center_transform_).position, angle);

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
                on_voxel_damaged(c, r);

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
                        // направление от центра взрыва к уничтоженному вокселю
                        glm::vec2 dir_voxel = dv.world_pos - position;
                        float len_voxel = glm::length(dir_voxel);

                        if (len_voxel < 0.001f) continue;
                        dir_voxel /= len_voxel;

                        // цель с наименьшим углом
                        float max_dot = -2.0f;
                        VoxelObject* best_target = nullptr;

                        for (const auto& t : targets) {
                            glm::vec2 dir_cm = t.cm - position;
                            float len_cm = glm::length(dir_cm);

                            if (len_cm < 0.001f) {
                                max_dot = 2.0f;
                                best_target = t.obj;
                                break;
                            }

                            dir_cm /= len_cm;

                            float dot = glm::dot(dir_voxel, dir_cm);
                            if (dot > max_dot) {
                                max_dot = dot;
                                best_target = t.obj;
                            }
                        }

                        // импульс
                        if (best_target) {
                            float magnitude = explosion.impulse / (len_voxel * len_voxel);

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

    std::vector<std::unique_ptr<VoxelObject>> split_if_disconnected()
    {
        const int total = cols_ * rows_;
        std::vector component(static_cast<size_t>(total), -1);
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
                    if (!voxels_[static_cast<size_t>(ni)].is_alive()) continue;

                    component[static_cast<size_t>(ni)] = comp_id;
                    q.push(ni);
                }
            }
        };

        for (int i = 0; i < total; ++i)
        {
            if (!voxels_[static_cast<size_t>(i)].is_alive()) continue;
            if (component[static_cast<size_t>(i)] != -1) continue;
            bfs(i, num_components++);
        }

        if (num_components <= 1)
            return {};

        std::vector<int> comp_size(static_cast<size_t>(num_components), 0);
        for (int i = 0; i < total; ++i)
        {
            const int c = component[static_cast<size_t>(i)];
            if (c >= 0) ++comp_size[static_cast<size_t>(c)];
        }
        const int main_comp = static_cast<int>(
            std::max_element(comp_size.begin(), comp_size.end()) - comp_size.begin());

        std::vector<std::unique_ptr<VoxelObject>> fragments;
        fragments.reserve(static_cast<size_t>(num_components - 1));

        auto& repo = SpritesRepository::get_active();
        for (int comp_id = 0; comp_id < num_components; ++comp_id)
        {
            if (comp_id == main_comp) continue;

            glm::vec2 frag_center = global_position() + math::rotate_vec2(repo.get_transform(grid_center_transform_).position, global_rotation());
            const float& global_angle = get_angle();

            fragments.push_back(std::move(std::make_unique<VoxelObject>(
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

            for (int i = 0; i < total; ++i)
            {
                if (component[static_cast<size_t>(i)] != comp_id) continue;

                Voxel& src = voxels_[static_cast<size_t>(i)];
                Voxel& dst = frag.voxels_[static_cast<size_t>(i)];

                dst = src;
                dst.spriteIdx = repo.add_sprite(repo.data_from_sprite_idx(src.spriteIdx), frag.grid_center_transform_);
                repo.data_from_sprite_idx(dst.spriteIdx).transformIdx = frag.grid_center_transform_;

                destroy_voxel(src);
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

        recalculate_physic();

        return fragments;
    }

    void on_connect_init(void* context, EventInvokingFunction* func, size_t id) override
    {
        invokeFunctionContext_ = context;
        newEventFunc_ = func;
        onSceneId_ = id;
    }

    void sync_sprites()
    {
        auto& repo = SpritesRepository::get_active();
        for (auto& vx : voxels_)
        {
            if (!vx.has_sprite()) continue;
            SpriteData& sd = repo.data_from_sprite_idx(vx.spriteIdx);
            sd.position = vx.center;
        }
    }

    [[nodiscard]] float get_angle() const override
    {
        return SpritesRepository::get_active().get_transform(mass_center_transform_).angle;
    }
    void add_angle(float dAngle) override
    {
        SpritesRepository::get_active().get_transform(mass_center_transform_).angle += dAngle;
    }
    void set_angle(float angle) override
    {
        SpritesRepository::get_active().get_transform(mass_center_transform_).angle = angle;
    }

    void set_global_angle(float angle)
    {
        float global_angle = global_rotation();
        global_angle = std::fmod(global_angle, std::numbers::pi_v<float> * 2);
        add_angle(angle - global_angle);
    }

    [[nodiscard]] glm::vec2 get_position() const override
    {
        return SpritesRepository::get_active().get_transform(mass_center_transform_).position;
    }
    void add_position(glm::vec2 pos) override
    {
        SpritesRepository::get_active().get_transform(mass_center_transform_).position += pos;
    }
    void set_position(glm::vec2 pos) override
    {
        SpritesRepository::get_active().get_transform(mass_center_transform_).position = pos;
    }

    [[nodiscard]] float get_angular_velocity() const override
    {
        return angular_velocity_;
    }
    void add_angular_velocity(float dVelocity) override
    {
        angular_velocity_ += dVelocity;
    }
    void set_angular_velocity(float velocity) override
    {
        angular_velocity_ = velocity;
    }

    [[nodiscard]] glm::vec2 get_velocity() const override
    {
        return velocity_;
    }
    void add_velocity(glm::vec2 velocity) override
    {
        velocity_ += velocity;
    }
    void set_velocity(glm::vec2 velocity) override
    {
        velocity_ = velocity;
    }

    [[nodiscard]] float get_mass() const override
    {
        return mass_;
    }
    [[nodiscard]] float get_moment() const override
    {
        return inertia_moment_;
    }

    [[nodiscard]] size_t get_parent_texture() const override
    {
        return parent_texture_;
    }

    [[nodiscard]] const RecursiveHitbox& get_hitbox() const override
    {
        return *hitbox_;
    }

    // -----------------------------------------------------------------------
    //  Коллизия — Уровень 1: описывающая окружность
    //
    //  Не зависит от угла поворота — подходит как быстрый широкий тест.
    //  Радиус = половина диагонали AABB сетки вокселей.
    // -----------------------------------------------------------------------
    [[nodiscard]] float get_bounding_radius() const
    {
        const float hw = cols_ * params_.cell_size * 0.5f;
        const float hh = rows_ * params_.cell_size * 0.5f;
        return std::sqrt(hw * hw + hh * hh);
    }

    [[nodiscard]] bool bounding_circles_overlap(const VoxelObject& other) const
    {
        const glm::vec2 d = get_position() - other.get_position();
        const float dist2 = d.x * d.x + d.y * d.y;
        const float rsum  = get_bounding_radius() + other.get_bounding_radius();
        return dist2 < rsum * rsum;
    }

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

    void apply_impulse_at(glm::vec2 impulse, glm::vec2 world_pos) override
    {
        velocity_ += impulse / mass_;

        glm::vec2 r = world_pos - get_position();
        float torque = r.x * impulse.y - r.y * impulse.x;
        angular_velocity_ += torque / inertia_moment_;
    }

    void apply_static_impulse(const glm::vec2& impulse)
    {
        velocity_ += impulse / mass_;
    }

    void invoke_events_from_this(std::vector<std::unique_ptr<events::SceneEvent>> outside_events) override
    {
        newEventFunc_(invokeFunctionContext_, outside_events);
    }

#ifndef DEBUG_COLLISION
    void update(float dt) override
#else
    void update(float dt, bool debug_draw) override
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

    [[nodiscard]] size_t get_grid_transform() const
    {
        return grid_center_transform_;
    }

    [[nodiscard]] size_t get_mass_transform() const
    {
        return mass_center_transform_;
    }

    void set_parent_transform(int index) const
    {
        SpritesRepository::get_active().get_transform(mass_center_transform_).parentIndex = index;
    }

    [[nodiscard]] int get_parent_transform() const
    {
        return SpritesRepository::get_active().get_transform(grid_center_transform_).parentIndex;
    }

    [[nodiscard]] glm::vec2 global_position() const
    {
        return SpritesRepository::get_active().get_global_position(mass_center_transform_);
    }

    [[nodiscard]] float global_rotation() const
    {
        return SpritesRepository::get_active().get_global_rotation(mass_center_transform_);
    }

    [[nodiscard]] float local_rotation() const
    {
        return SpritesRepository::get_active().get_transform(mass_center_transform_).angle;
    }

    [[nodiscard]] size_t id() const override
    {
        return onSceneId_;
    }

#ifdef DEBUG_COLLISION
    void DebugDraw() const override
    {
        hitbox_->debugDraw(get_position(), get_angle());
    }
#endif

protected:
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
        hitbox_shift += SpritesRepository::get_active().get_transform(grid_center_transform_).position;

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
        cols_ = tex_.width;
        rows_ = tex_.height;

        if (cols_ <= 0 || rows_ <= 0) return;

        voxels_.resize(static_cast<size_t>(cols_ * rows_));

        const float cs      = params_.cell_size;
        const float pixel_u = (local_UV_.u1 - local_UV_.u0) / static_cast<float>(tex_.width);
        const float pixel_v = (local_UV_.v1 - local_UV_.v0) / static_cast<float>(tex_.height);
        const size_t tex_desc = reinterpret_cast<size_t>(&tex_);

        auto& repo = SpritesRepository::get_active();
        for (int row = 0; row < rows_; ++row)
        {
            for (int col = 0; col < cols_; ++col)
            {
                Voxel& vx = voxels_[static_cast<size_t>(row * cols_ + col)];

                vx.center = {
                    (col + 0.5f - cols_ * 0.5f) * cs,
                    (row + 0.5f - rows_ * 0.5f) * cs
                };
                vx.hp     = params_.default_hp;
                vx.status = VoxelStatus::Absent;

                if (tex_.colorData != nullptr)
                {
                    const size_t pixel_idx = static_cast<size_t>(row * tex_.width + col);
                    if (tex_.colorData[pixel_idx * 4 + 3] <= alpha_threshold) continue;
                }

                vx.status = VoxelStatus::Solid;

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

                vx.spriteIdx = repo.add_sprite(sd);
                repo[vx.spriteIdx].textureDesc = tex_desc;
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

        auto& repo = SpritesRepository::get_active();
        TransformNode& mass_center = repo.get_transform(mass_center_transform_);
        TransformNode& grid_center = repo.get_transform(grid_center_transform_);

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

        build_local_chunks();
        return true;
    }

    static void destroy_voxel(Voxel& vx)
    {
        if (vx.has_sprite()) {
            SpritesRepository::get_active().get_data_pointer(vx.spriteIdx)->depth = 10;
        }
        vx.hp = 0;
        vx.status = VoxelStatus::Destroyed;
    }

protected:
    // вызывается для каждого вокселя, чье здоровье изменилось в area_damage
    virtual void on_voxel_damaged(int col, int row) {}
};
