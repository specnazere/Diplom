//
// Created by A on 08.06.2026.
//
module;

#include <vector>
#include <glm/glm.hpp>

#include "graphics/uv_rect.h"

export module projectile;

import texture;
import sprite_repository;
import sprite;

export
struct projectile
{
private:
    SpritesRepository& repo_;

public:
    const texture& tex;
    size_t         texture_id;
    size_t         sprite;

    glm::vec2 velocity = {0, 0};

    projectile(
    SpritesRepository& repository,
    const texture& tex,
    const size_t texture_id,
    const uv_rect& local_UV,
    const glm::vec2& position = {},
    const glm::vec2& velocity = {})
    : repo_(repository)
    , tex(tex)
    , texture_id(texture_id)
    , velocity(velocity)
    {
        SpriteData sd;
        sd.width      = tex.width;
        sd.height     = tex.height;
        sd.angle      = 0.f;
        sd.depth      = 0;
        sd.position_x = position.x;
        sd.position_y = position.y;
        sd.paralax    = 1;
        sd.uvRect     = local_UV;
        sd.transformIdx = ~static_cast<size_t>(0);

        sprite = repo_.addSprite(sd, sd.depth);
    }

    projectile(const projectile& other)
    : repo_(other.repo_)
    , tex(other.tex)
    , texture_id(other.texture_id)
    {
        SpriteData sd;
        sd.width      = tex.width;
        sd.height     = tex.height;
        sd.angle      = 0.f;
        sd.depth      = 0;
        glm::vec2 pos = other.get_position();
        sd.position_x = pos.x;
        sd.position_y = pos.y;
        sd.paralax    = 1;
        sd.uvRect     = other.getUV();
        sd.transformIdx = ~static_cast<size_t>(0);

        sprite = repo_.addSprite(sd, sd.depth);
    }

    void update(const float& dt)
    {
        add_position(velocity * dt);
    }

    void add_rotation(const float& dAngle)
    {
        repo_.getDataPointer(sprite)->angle += dAngle;
    }
    void set_rotation(const float& angle)
    {
        repo_.getDataPointer(sprite)->angle = angle;
    }

    float get_angle() const
    {
        return repo_.getDataPointer(sprite)->angle;
    }

    void add_position(const glm::vec2& pos)
    {
        repo_.getDataPointer(sprite)->position_x += pos.x;
        repo_.getDataPointer(sprite)->position_y += pos.y;
    }

    void set_position(const glm::vec2& pos)
    {
        repo_.getDataPointer(sprite)->position_x = pos.x;
        repo_.getDataPointer(sprite)->position_y = pos.y;
    }

    glm::vec2 get_position() const
    {
        return {repo_.getDataPointer(sprite)->position_x, repo_.getDataPointer(sprite)->position_y};
    }

    float rotation() const
    {
        return repo_.getDataPointer(sprite)->angle;
    }

private:
    const uv_rect& getUV() const
    {
        return repo_.getDataPointer(sprite)->uvRect;
    }
};
