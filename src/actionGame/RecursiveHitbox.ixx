//
// Created by A on 10.06.2026.
//
module;

#include <glm/glm.hpp>

#include <cmath>
#include <vector>
#include <functional>
#include <iostream>
#include <optional>
#include <ostream>
#include <stdexcept>

#ifdef DEBUG_COLLISION
#include "imgui/imgui.h"
import camera;
#endif

export module recursive_hitbox;

namespace collision_math
{
    [[nodiscard]] inline glm::vec2 rotate_vec(glm::vec2 v, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return { v.x * c - v.y * s,
                 v.x * s + v.y * c };
    }

    [[nodiscard]] inline glm::vec2 local_to_world(glm::vec2 local, glm::vec2 pos, float angle)
    {
        return pos + rotate_vec(local, angle);
    }

    [[nodiscard]] inline glm::vec2 world_to_local(glm::vec2 world, glm::vec2 pos, float angle)
    {
        return rotate_vec(world - pos, -angle);
    }

    [[nodiscard]] inline glm::vec2 a_local_to_b_local(glm::vec2 p,
                                         glm::vec2 posA, float angleA,
                                         glm::vec2 posB, float angleB)
    {
        return world_to_local(local_to_world(p, posA, angleA), posB, angleB);
    }

    bool rects_intersect(
        glm::vec2 center_a_local_b,
        float angle_a_rel_b,
        glm::vec2 size_a,
        glm::vec2 size_b)
    {
        glm::vec2 h_a = size_a * 0.5f;
        glm::vec2 h_b = size_b * 0.5f;

        float c = std::cos(angle_a_rel_b);
        float s = std::sin(angle_a_rel_b);
        float abs_c = std::abs(c);
        float abs_s = std::abs(s);

        float r_a = h_a.x * abs_c + h_a.y * abs_s;
        if (std::abs(center_a_local_b.x) > r_a + h_b.x) return false;

        r_a = h_a.x * abs_s + h_a.y * abs_c;
        if (std::abs(center_a_local_b.y) > r_a + h_b.y) return false;

        float r_b = h_b.x * abs_c + h_b.y * abs_s;
        if (std::abs(center_a_local_b.x * c + center_a_local_b.y * s) > h_a.x + r_b) return false;

        r_b = h_b.x * abs_s + h_b.y * abs_c;
        if (std::abs(center_a_local_b.x * (-s) + center_a_local_b.y * c) > h_a.y + r_b) return false;

        return true;
    }

    glm::vec2 get_contact_point(
        glm::vec2 center_a_local_b,
        float angle_a_rel_b,
        glm::vec2 size_a,
        glm::vec2 size_b)
    {
        glm::vec2 h_a = size_a * 0.5f;
        glm::vec2 h_b = size_b * 0.5f;
        float c = std::cos(angle_a_rel_b);
        float s = std::sin(angle_a_rel_b);
        float abs_c = std::abs(c);
        float abs_s = std::abs(s);

        // хранениe данных осей
        float overlaps[4];
        float r_a_x = h_a.x * abs_c + h_a.y * abs_s;
        float r_a_y = h_a.x * abs_s + h_a.y * abs_c;
        float dot_ax = center_a_local_b.x * c + center_a_local_b.y * s;
        float dot_ay = -center_a_local_b.x * s + center_a_local_b.y * c;
        float r_b_x = h_b.x * abs_c + h_b.y * abs_s;
        float r_b_y = h_b.x * abs_s + h_b.y * abs_c;

        // глубина проникновения для всех осей
        overlaps[0] = (r_a_x + h_b.x) - std::abs(center_a_local_b.x); // Ось X прямоугольника B
        overlaps[1] = (r_a_y + h_b.y) - std::abs(center_a_local_b.y); // Ось Y прямоугольника B
        overlaps[2] = (h_a.x + r_b_x) - std::abs(dot_ax);             // Ось X прямоугольника A
        overlaps[3] = (h_a.y + r_b_y) - std::abs(dot_ay);             // Ось Y прямоугольника A

        // ось с минимальным проникновением
        int min_idx = 0;
        float min_overlap = overlaps[0];
        for (int i = 1; i < 4; ++i) {
            if (overlaps[i] < min_overlap) {
                min_overlap = overlaps[i];
                min_idx = i;
            }
        }

        glm::vec2 contact_local_b;

        // середина отрезка пересечения на опорной грани
        if (min_idx == 0) {
            contact_local_b.x = (center_a_local_b.x > 0 ? 1.0f : -1.0f) * h_b.x;
            float y_min = std::max(-h_b.y, center_a_local_b.y - r_a_y);
            float y_max = std::min(h_b.y, center_a_local_b.y + r_a_y);
            contact_local_b.y = (y_min + y_max) * 0.5f;
        }
        else if (min_idx == 1) {
            contact_local_b.y = (center_a_local_b.y > 0 ? 1.0f : -1.0f) * h_b.y;
            float x_min = std::max(-h_b.x, center_a_local_b.x - r_a_x);
            float x_max = std::min(h_b.x, center_a_local_b.x + r_a_x);
            contact_local_b.x = (x_min + x_max) * 0.5f;
        }
        else if (min_idx == 2) {
            float nax = dot_ax > 0 ? -1.0f : 1.0f;
            glm::vec2 pt_a;
            pt_a.x = nax * h_a.x;

            float ay_min = std::max(-h_a.y, -dot_ay - r_b_y);
            float ay_max = std::min(h_a.y, -dot_ay + r_b_y);
            pt_a.y = (ay_min + ay_max) * 0.5f;

            // из локальных координат A в локальные координаты B
            contact_local_b.x = pt_a.x * c - pt_a.y * s + center_a_local_b.x;
            contact_local_b.y = pt_a.x * s + pt_a.y * c + center_a_local_b.y;
        }
        else {
            float nay = dot_ay > 0 ? -1.0f : 1.0f;
            glm::vec2 pt_a;
            pt_a.y = nay * h_a.y;

            float ax_min = std::max(-h_a.x, -dot_ax - r_b_x);
            float ax_max = std::min(h_a.x, -dot_ax + r_b_x);
            pt_a.x = (ax_min + ax_max) * 0.5f;

            // из локальных координат A в локальные координаты B
            contact_local_b.x = pt_a.x * c - pt_a.y * s + center_a_local_b.x;
            contact_local_b.y = pt_a.x * s + pt_a.y * c + center_a_local_b.y;
        }

        return contact_local_b;
    }
}

export
struct GridBoundary
{
    size_t x1, y1;
    size_t x2, y2;
};

export
class RecursiveHitbox
{
public:
    using haveSolidFunc = std::function<bool(const GridBoundary& boundary)>;
    inline static haveSolidFunc standardFunc = [](const GridBoundary& boundary) { return true; };

    struct CollisionCheckerInput
    {
        const RecursiveHitbox& hitbox;
        const glm::vec2 position;
        const float angle;
    };

private:
    std::vector<RecursiveHitbox> hitboxes_;

    GridBoundary boundary_;

    glm::vec2 size_;
    glm::vec2 center_;
    glm::vec2 shift_;
    float signature;

    bool is_solid_ = false;
    bool fully_solid_ = false;
    bool leaf_ = false;

#ifdef DEBUG_COLLISION
    // отрисовка залитого прямоугольника
    static void drawRect(const glm::vec2& world_pos, float angle, const glm::vec2& size, ImU32 color, ImU32 fill = 0)
    {
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();

        glm::vec2 half_size = size * 0.5f;
        glm::vec2 corners[4] = {
            {-half_size.x, -half_size.y},
            { half_size.x, -half_size.y},
            { half_size.x,  half_size.y},
            {-half_size.x,  half_size.y}
        };

        ImVec2 screen_corners[4];
        for (int i = 0; i < 4; ++i) {
            glm::vec2 rotated = collision_math::rotate_vec(corners[i], angle);
            glm::vec2 world_point = world_pos + rotated;
            glm::vec2 screen_point = camera::world_to_screen(world_point);
            screen_corners[i] = ImVec2(screen_point.x, screen_point.y);
        }

        if (fill != 0) {
            draw_list->AddConvexPolyFilled(screen_corners, 4, fill);
        }
        if (color != 0) {
            draw_list->AddPolyline(screen_corners, 4, color, ImDrawFlags_Closed, 1.5f);
        }
    }
#endif

public:
    RecursiveHitbox(
        const GridBoundary& boundary,
        size_t base,
        glm::vec2 initial_size,
        glm::vec2 center,
        const haveSolidFunc& have_solid_func = standardFunc,
        bool update_solindess = true,
        glm::vec2 shift = {})
        : boundary_(boundary)
        , size_(initial_size)
        , center_(center)
        , shift_(shift)
    {
        signature = std::sqrt(initial_size.x * initial_size.x + initial_size.y * initial_size.y);

        size_t grid_width = boundary.x2 - boundary.x1;
        size_t grid_height = boundary.y2 - boundary.y1;

        if (grid_width == 1 && grid_height == 1) {
            leaf_ = true;
        }
        else {
            std::vector<size_t> widths;
            if (grid_width < base) {
                widths = std::vector<size_t>(grid_width, 1);
            }
            else {
                size_t base_width = grid_width / base;
                widths = std::vector<size_t>(base, base_width);

                size_t leftover_width = grid_width - base_width * base;
                for (size_t& width : widths) {
                    if (leftover_width-- > 0) {
                        width += 1;
                    }
                    else {
                        break;
                    }
                }
            }

            std::vector<size_t> heights;
            if (grid_height < base) {
                heights = std::vector<size_t>(grid_height, 1);
            }
            else {
                size_t base_height = grid_height / base;
                heights = std::vector<size_t>(base, base_height);

                size_t leftover_height = grid_height - base_height * base;
                for (size_t& height : heights) {
                    if (leftover_height-- > 0) {
                        height += 1;
                    }
                    else {
                        break;
                    }
                }
            }

            hitboxes_.reserve(widths.size() * heights.size());

            glm::vec2 cell_size = {initial_size.x / static_cast<float>(grid_width), initial_size.y / static_cast<float>(grid_height)};
            glm::vec2 halved_grid_size{grid_width * 0.5f, grid_height * 0.5f};

            size_t cummulative_width = 0;
            for (size_t& width : widths) {

                size_t cummulative_height = 0;
                for (size_t& height : heights) {

                    hitboxes_.emplace_back(
                        GridBoundary{
                            boundary_.x1 + cummulative_width,
                            boundary_.y1 + cummulative_height,
                            boundary_.x1 + cummulative_width + width,
                            boundary_.y1 + cummulative_height + height
                        },
                        base,
                        glm::vec2(
                            cell_size.x * static_cast<float>(width),
                            cell_size.y * static_cast<float>(height)),
                        center_,
                        have_solid_func,
                        false,
                        glm::vec2(
                            (static_cast<float>(cummulative_width) + width * 0.5f - halved_grid_size.x) * cell_size.x,
                            (static_cast<float>(cummulative_height) + height * 0.5f - halved_grid_size.y) * cell_size.y) + shift_);

                    cummulative_height += height;
                }

                cummulative_width += width;
            }
        }

        if (update_solindess) {
            update_solidness(have_solid_func);
        }
    }

    void update_solidness(const haveSolidFunc& have_solid_func)
    {
        if (leaf_) {
            if (have_solid_func(boundary_)) {
                is_solid_ = true;
                fully_solid_ = true;
            }
        }
        else {
            is_solid_ = false;

            fully_solid_ = true;
            for (RecursiveHitbox& hitbox : hitboxes_) {
                hitbox.update_solidness(have_solid_func);
                if (hitbox.is_solid_) {
                    is_solid_ = true;
                }
                if (!hitbox.fully_solid_) {
                    fully_solid_ = false;
                }
            }
        }
    }

    static std::optional<glm::vec2> checkCollision(const CollisionCheckerInput& first, const CollisionCheckerInput& second)
    {
        if (!(first.hitbox.is_solid_ && second.hitbox.is_solid_))
            return {};

        const glm::vec2 posA = first.position + collision_math::rotate_vec(first.hitbox.shift_ + first.hitbox.center_, first.angle);
        const glm::vec2 posB = second.position + collision_math::rotate_vec(second.hitbox.shift_ + second.hitbox.center_, second.angle);
        const glm::vec2 dPos = posB - posA;
        const float& sigA = first.hitbox.signature;
        const float& sigB = second.hitbox.signature;

        if (dPos.x * dPos.x + dPos.y * dPos.y > (sigA + sigB) * (sigA + sigB))
            return {};

        auto final_contact = glm::vec2(0);
        float contacts = 0;

        const glm::vec2& sizeA = first.hitbox.size_;
        const glm::vec2& sizeB = second.hitbox.size_;
        const float&     angA  = first.angle;
        const float&     angB  = second.angle;

        const float rel_angle = angA - angB;

        // ИСПРАВЛЕНИЕ 1: Переводим вектор из мирового пространства в локальное пространство B
        glm::vec2 center_a_local_b = collision_math::rotate_vec(posA - posB, -angB);

        if (collision_math::rects_intersect(center_a_local_b, rel_angle, sizeA, sizeB)) {
            int mask = first.hitbox.fully_solid_ << 1 | second.hitbox.fully_solid_;

            auto add_contact = [&](const glm::vec2& local_contact_pos, const glm::vec2& center, float angle) {
                glm::vec2 world_contact = center + collision_math::rotate_vec(local_contact_pos, angle);
                final_contact += world_contact;
                contacts++;
            };

            switch (mask) {
                case 0b00: {
                    for (const RecursiveHitbox& hitbox_a : first.hitbox.hitboxes_) {
                        for (const RecursiveHitbox& hitbox_b : second.hitbox.hitboxes_) {
                            std::optional<glm::vec2> contact = checkCollision(
                                {hitbox_a, first.position, angA},
                                {hitbox_b, second.position, angB}
                            );
                            if (contact.has_value()) {
                                final_contact += contact.value();
                                contacts++;
                            }
                        }
                    }
                    break;
                }
                case 0b01:
                {
                    for (const RecursiveHitbox& hitbox_a : first.hitbox.hitboxes_) {
                        std::optional<glm::vec2> contact = checkCollision(
                            {hitbox_a, first.position, angA},
                            second
                        );
                        if (contact.has_value()) {
                            final_contact += contact.value();
                            contacts++;
                        }
                    }
                    break;
                }
                case 0b10:
                {
                    for (const RecursiveHitbox& hitbox_b : second.hitbox.hitboxes_) {
                        std::optional<glm::vec2> contact = checkCollision(
                            first,
                            {hitbox_b, second.position, angB}
                        );
                        if (contact.has_value()) {
                            final_contact += contact.value();
                            contacts++;
                        }
                    }
                    break;
                }
                case 0b11:
                {
                    glm::vec2 local_contact = collision_math::get_contact_point(center_a_local_b, rel_angle, sizeA, sizeB);
                    add_contact(local_contact, posB, angB);

#ifdef DEBUG_COLLISION
                    drawRect(posA, angA, sizeA, 0, IM_COL32(255, 0, 0, 100));
                    drawRect(posB, angB, sizeB, 0, IM_COL32(0, 0, 255, 100));
#endif
                    break;
                }
            }
        }

        if (contacts > 0) {
            return final_contact / contacts;
        }

        return {};
    }

#ifdef DEBUG_COLLISION
    void debugDraw(const glm::vec2& world_pos, float angle, ImU32 color = IM_COL32(255, 0, 0, 255)) const
    {
        if (fully_solid_) {
            drawRect(world_pos + collision_math::rotate_vec(shift_ + center_, angle), angle, size_, color, 0);
        }

        if (!fully_solid_) {
            for (const auto& child : hitboxes_) {
                glm::vec2 child_world_pos = world_pos;
                child.debugDraw(child_world_pos, angle, IM_COL32(0, 255, 0, 100));
            }
        }
    }
#endif

    [[nodiscard]] glm::vec2 get_size() const
    {
        return size_;
    }
};