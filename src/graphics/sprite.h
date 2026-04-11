//
// Created by A on 27.01.2026.
//

#ifndef RENDEROBJECT_H
#define RENDEROBJECT_H



#include <string>
#include <glm/fwd.hpp>

#include "texture_atlas.h"

class sprite {
public:
    struct alignas(16) gpu_instance_data {
        glm::float32 width;
        glm::float32 height;

        glm::float32 angle;

        glm::float32 position_x;
        glm::float32 position_y;

        glm::float32 paralax = 1;

        texture_atlas::uv_rect uv;
    };

private:
    gpu_instance_data instance_data_{};
    bool to_remove_ = false;
    int depth_{};
    unsigned int texture_handler_{};

public:

    explicit sprite(const std::string& texture_name);
    explicit sprite(const char* texture_name);
    explicit sprite(const unsigned int& texture_handler);

    void set_width(float const& width);
    void set_height(float const& height);
    void set_angle(float const& angle);
    void set_position(float const& position_x, float const& position_y);
    void set_texture(const std::string& new_texture_name);
    void set_texture(const char* new_texture_name);
    void set_texture(const unsigned int& new_texture_handler);

    void load_uv_rect(const texture_atlas* atlas);

    void markDead();
    [[nodiscard]] bool getDeadMark() const;

    [[nodiscard]] const gpu_instance_data& get_gpu_instance_data() const;

    [[nodiscard]] int getDepth() const;
};


#endif //RENDEROBJECT_H
