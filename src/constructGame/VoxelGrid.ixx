//
// Created by A on 16.06.2026.
//
module;

#include <algorithm>
#include <memory>
#include <vector>

#include "graphics/uv_rect.h"

export module voxel_grid;

export import texture;
export import sprite_repository;
export import camera;
export import voxel_health_protocol;

export
enum overlays : uint8_t
{
    healthOverlay = 0,
    textureOverlay,
    moduleOverlay,
};

export
struct overlay_colors
{
    glm::u8vec4 health_color = glm::u8vec4{255};
    glm::u8vec4 texture_color = glm::u8vec4{255};
    glm::u8vec4 module_color = glm::u8vec4{255};

    glm::u8vec4& operator[](const overlays overlay)
    {
        switch (overlay) {
            case textureOverlay: return texture_color;
            case moduleOverlay: return module_color;
            default: return health_color;
        }
    }
};

export
struct Voxel
{
    float max_health{};
    float health{};
    overlay_colors overlays;
    size_t sprite_index{};
};

export
class VoxelGrid
{
public:
    static constexpr glm::u8vec4 min_health_color = {255, 0, 0, 255};
    static constexpr glm::u8vec4 med_health_color = {255, 255, 0, 255};
    static constexpr glm::u8vec4 max_health_color = {0, 255, 0, 255};

    static constexpr glm::u8vec4 no_module_color = {100, 100, 100, 255};

private:
    size_t cols_;
    size_t rows_;

    std::vector<Voxel> voxels_;

    size_t main_transform_;

    overlays current_overlay_;

public:

    VoxelGrid(const texture& tex, uv_rect blank_uv, float health)
    : current_overlay_()
    {
        cols_ = tex.width;
        rows_ = tex.height;

        if (cols_ <= 0 || rows_ <= 0) return;

        SpritesRepository& repo = SpritesRepository::get_active();
        main_transform_ = repo.createTransform();

        voxels_.resize(cols_ * rows_);

        const size_t tex_desc = reinterpret_cast<size_t>(&tex);

        for (int row = 0; row < rows_; ++row)
            for (int col = 0; col < cols_; ++col) {
                Voxel& vx = voxels_[row * cols_ + col];

                const size_t pixel_idx = static_cast<size_t>(row * tex.width + col);
                if (tex.colorData != nullptr) {
                    if (tex.colorData[pixel_idx * 4 + 3] <= 0.f) continue;
                }

                vx.health = health;
                vx.max_health = health;
                vx.overlays.health_color = max_health_color;
                vx.overlays.texture_color.r = tex.colorData[pixel_idx * 4 + 0];
                vx.overlays.texture_color.g = tex.colorData[pixel_idx * 4 + 1];
                vx.overlays.texture_color.b = tex.colorData[pixel_idx * 4 + 2];
                vx.overlays.texture_color.a = tex.colorData[pixel_idx * 4 + 3];
                vx.overlays.module_color = no_module_color;

                glm::vec2 position = {
                    static_cast<float>(col) - static_cast<float>(tex.width) * 0.5f,
                    static_cast<float>(row) - static_cast<float>(tex.height) * 0.5f,
                };

                SpriteData sd;
                sd.width = 1.f;
                sd.height = 1.f;
                sd.angle = 0.f;
                sd.depth = 0.f;
                sd.position = position;
                sd.paralax = 1.f;
                sd.uvRect.u0 = blank_uv.u0;
                sd.uvRect.v0 = blank_uv.v0;
                sd.uvRect.u1 = blank_uv.u1;
                sd.uvRect.v1 = blank_uv.v1;
                sd.transformIdx = main_transform_;
                sd.color = {255, 255, 255, 255};

                vx.sprite_index = repo.addSprite(sd);
                repo[vx.sprite_index].textureDesc = tex_desc;
            }

        float scale = std::max(
                          tex.width / static_cast<float>(camera::get_frame_width()),
                          tex.height / static_cast<float>(camera::get_frame_height())
                      ) * 2;

        camera::set_scale(scale);

        set_overlay(current_overlay_);

        return;
    }

    // В VoxelGrid или SceneManager
    static std::unique_ptr<VoxelGrid> create_from_snapshot(
        const texture& tex,
        uv_rect blank_uv,
        const std::vector<VoxelStateUpdate>& updates)
    {

        // Создаём VoxelGrid с найденной текстурой
        auto grid = std::make_unique<VoxelGrid>(tex, blank_uv, 1.f);

        // Применяем здоровье из снапшота
        SpritesRepository& repo = SpritesRepository::get_active();
        for (const auto& upd : updates) {
            if (upd.voxel_idx >= grid->voxels_.size()) continue;

            Voxel& vx = grid->voxels_[upd.voxel_idx];
            vx.health = upd.health;
            vx.max_health = vx.health;

            vx.overlays.health_color = health_color(
                static_cast<float>(vx.health),
                static_cast<float>(vx.max_health)
            );

            if (grid->current_overlay_ == healthOverlay) {
                repo.dataFromSpriteIdx(vx.sprite_index).color = vx.overlays.health_color;
            }
        }

        return grid;
    }

    void set_overlay(overlays overlay)
    {
        SpritesRepository& repo = SpritesRepository::get_active();

        for (int row = 0; row < rows_; ++row)
        for (int col = 0; col < cols_; ++col) {
            Voxel& vx = voxels_[row * cols_ + col];

             repo.dataFromSpriteIdx(vx.sprite_index).color = vx.overlays[overlay];
        }
    }

    static glm::u8vec4 health_color(float health, float max_health)
    {

        float diff = health / max_health;
        diff = std::clamp(diff, 0.0f, 1.0f);
        if (diff == 0.f)
            return {0, 0, 0, 255};

        if (diff <= 0.5f)
        {
            float t = diff * 2.0f;
            return {
                static_cast<uint8_t>(min_health_color.r + (med_health_color.r - min_health_color.r) * t),
                static_cast<uint8_t>(min_health_color.g + (med_health_color.g - min_health_color.g) * t),
                static_cast<uint8_t>(min_health_color.b + (med_health_color.b - min_health_color.b) * t),
                255
            };
        } else {
            float t = (diff - 0.5f) * 2.0f;
            return {
                static_cast<uint8_t>(med_health_color.r + (max_health_color.r - med_health_color.r) * t),
                static_cast<uint8_t>(med_health_color.g + (max_health_color.g - med_health_color.g) * t),
                static_cast<uint8_t>(med_health_color.b + (max_health_color.b - med_health_color.b) * t),
                255
            };
        }
        return {};
    }

    void apply_network_update(const VoxelBatchUpdate& batch)
    {
        SpritesRepository& repo = SpritesRepository::get_active();

        for (uint32_t i = 0; i < batch.count; ++i) {
            const auto& upd = batch.updates[i];
            if (upd.voxel_idx >= voxels_.size()) continue;

            Voxel& vx = voxels_[upd.voxel_idx];

            // Применяем здоровье (приводим float к int, как у тебя в VoxelGrid)
            vx.health = static_cast<int>(upd.health);

            // Пересчитываем цвет оверлея здоровья
            vx.overlays.health_color = health_color(static_cast<float>(vx.health), static_cast<float>(vx.max_health));

            // Если сейчас включен оверлей здоровья, обновляем цвет спрайта
            if (current_overlay_ == healthOverlay) {
                repo.dataFromSpriteIdx(vx.sprite_index).color = vx.overlays.health_color;
            }
        }
    }
};
