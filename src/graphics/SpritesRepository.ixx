//
// Created by A on 17.04.2026.
//
module;

#include "glm/glm.hpp"

#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>

export module sprite_repository;

export import atlas;
export import sprite;
export import transform_node;

export
class SpritesRepository
{
private:
    std::vector<Sprite> sprites_;
    std::vector<SpriteData> sprite_data_;
    std::vector<TransformNode> transforms_;
    std::queue<size_t> free_sprite_indices_;
    std::queue<size_t> free_transform_indices_;

public:
    Atlas atlas;

    size_t create_transform(int parentIndex = -1) {
        size_t idx;
        if (!free_transform_indices_.empty())
            {
            idx = free_transform_indices_.front();
            free_transform_indices_.pop();
            transforms_[idx] = TransformNode{};
        } else {
            idx = transforms_.size();
            transforms_.push_back(TransformNode{});
        }

        transforms_[idx].parentIndex = parentIndex;
        return idx;
    }

    void free_transform(size_t transformIdx) {
        if (transformIdx >= transforms_.size()) return;

        // Опционально: сбросить родителя у детей? Нет, это дорого.
        // Просто возвращаем индекс в пул.
        free_transform_indices_.push(transformIdx);

        // Помечаем как невалидный (например, родителем -2 или специальным флагом, если нужно)
        transforms_[transformIdx].parentIndex = -2; // Маркер "удалено"
    }

    void set_transform_local(size_t transformIdx, float x, float y, float angle) {
        if (transformIdx >= transforms_.size()) return;
        transforms_[transformIdx].position.x = x;
        transforms_[transformIdx].position.y = y;
        transforms_[transformIdx].angle = angle;
    }

    glm::vec3 compute_global_transform(size_t transformIdx) const {
        if (transformIdx >= transforms_.size()) return {0,0,0};

        std::vector<const TransformNode*> path;
        size_t currentIdx = transformIdx;
        int safety = 0;

        while (currentIdx < transforms_.size() && transforms_[currentIdx].parentIndex >= 0 && safety < 100) {
            path.push_back(&transforms_[currentIdx]);
            currentIdx = static_cast<size_t>(transforms_[currentIdx].parentIndex);
        }

        if (currentIdx < transforms_.size() && transforms_[currentIdx].parentIndex == -1) {
             path.push_back(&transforms_[currentIdx]);
        }

        float globalX = 0;
        float globalY = 0;
        float globalAngle = 0;

        float currentAccumulatedAngle = 0;
        float currentAccumulatedX = 0;
        float currentAccumulatedY = 0;

        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            const TransformNode* node = *it;

            float cosA = std::cos(currentAccumulatedAngle);
            float sinA = std::sin(currentAccumulatedAngle);

            float rotatedLocalX = node->position.x * cosA - node->position.y * sinA;
            float rotatedLocalY = node->position.x * sinA + node->position.y * cosA;

            currentAccumulatedX += rotatedLocalX;
            currentAccumulatedY += rotatedLocalY;
            currentAccumulatedAngle += node->angle;
        }

        return {currentAccumulatedX, currentAccumulatedY, currentAccumulatedAngle};
    }

    size_t add_sprite(SpriteData data, size_t transformIdx = ~size_t{0})
    {
        const size_t new_data_idx = sprite_data_.size();
        sprite_data_.push_back(data);

        size_t sprite_idx;
        if (!free_sprite_indices_.empty())
            {
            sprite_idx = free_sprite_indices_.front();
            free_sprite_indices_.pop();
            sprites_[sprite_idx] = Sprite{};
        } else {
            sprite_idx = sprites_.size();
            sprites_.emplace_back();
        }

        Sprite& s = sprites_[sprite_idx];
        s.dataIdx     = new_data_idx;
        s.transformIdx = transformIdx;

        return sprite_idx;
    }

    void free_sprite(size_t sprite_idx)
    {
        if (sprite_idx >= sprites_.size()) return;

        Sprite& spr = sprites_[sprite_idx];
        if (!spr.is_valid()) return;

        const size_t data_idx_to_remove = spr.dataIdx;
        const size_t last_data_idx      = sprite_data_.size() - 1;

        if (data_idx_to_remove != last_data_idx)
        {
            sprite_data_[data_idx_to_remove] = sprite_data_[last_data_idx];

            for (Sprite& s : sprites_) {
                if (s.is_valid() && s.dataIdx == last_data_idx) {
                    s.dataIdx = data_idx_to_remove;
                }
            }
        }
        sprite_data_.pop_back();

        spr.dataIdx = ~size_t{0};
        spr.transformIdx = ~size_t{0};
        free_sprite_indices_.push(sprite_idx);
    }

    [[nodiscard]] const SpriteData* get_data_pointer() const
    {
        return sprite_data_.empty() ? nullptr : sprite_data_.data();
    }

    [[nodiscard]] SpriteData* get_data_pointer(size_t sprite_idx)
    {
        return &sprite_data_[sprites_[sprite_idx].dataIdx];
    }

    [[nodiscard]] size_t get_data_size() const { return sprite_data_.size(); }

    [[nodiscard]] const TransformNode* get_transforms_pointer() const {
        return transforms_.empty() ? nullptr : transforms_.data();
    }

    [[nodiscard]] size_t get_transforms_count() const {
        return transforms_.size();
    }

    [[nodiscard]] TransformNode& get_transform(size_t local_transform_)
    {
        return transforms_[local_transform_];
    }

    [[nodiscard]] SpriteData& data_from_sprite_idx(size_t index)
    {
        return sprite_data_[sprites_[index].dataIdx];
    }

    [[nodiscard]] float get_global_rotation(size_t transform) const
    {
        float rotation = 0;

        while (transform != ~size_t{0}) {
            auto& transform_ref = transforms_[transform];
            rotation += transform_ref.angle;
            transform = transform_ref.parentIndex;
        }
        return rotation;
    }

    [[nodiscard]] glm::vec2 get_global_position(size_t transform) const
    {
        glm::vec2 position{};
        glm::vec2 rotated_position{};

        while (transform != ~size_t{0}) {
            auto& transform_ref = transforms_[transform];
            float cosA = std::cosf(transform_ref.angle);
            float sinA = std::sinf(transform_ref.angle);
            rotated_position.x = position.x * cosA - position.y * sinA;
            rotated_position.y = position.x * sinA + position.y * cosA;
            rotated_position += transform_ref.position;
            position = rotated_position;
            transform = transform_ref.parentIndex;
        }
        return position;
    }

    [[nodiscard]] Sprite& operator[](size_t index) { return sprites_[index]; }

    [[nodiscard]] const Sprite& operator[](size_t index) const { return sprites_[index]; }


// static:
    inline static SpritesRepository* active_repo;

    [[nodiscard]] static SpritesRepository& get_active()
    {
        return *active_repo;
    }
    static void set_active(SpritesRepository* repo)
    {
        active_repo = repo;
    }
};