//
// Created by A on 27.01.2026.
//

#include "sprite.h"
#include "limits"

#include "renderer.h"
#include "texture_manager.h"

sprite::sprite(std::string const &texture_name) {
    *this = sprite(texture_name.c_str());
}

sprite::sprite(const char *texture_name) {
    set_texture(texture_name);
    renderer::register_sprite(this);
    set_position(0, 0);
    set_angle(0);
}

sprite::sprite(const unsigned int &texture_handler) {
    set_texture(texture_handler);
    renderer::register_sprite(this);
    set_position(0, 0);
    set_angle(0);
}

inline void sprite::set_width(float const &width) {
    this->instance_data_.width = width;
}

inline void sprite::set_height(float const &height) {
    this->instance_data_.height = height;
}

inline void sprite::set_angle(float const &angle) {
    this->instance_data_.angle = angle;
}

void sprite::set_position(float const &position_x, float const &position_y) {
    this->instance_data_.position_x = -position_x;
    this->instance_data_.position_y = -position_y;
}

inline void sprite::set_texture(std::string const &new_texture_name) {
    set_texture(new_texture_name.c_str());
}

void sprite::set_texture(const char *new_texture_name) {
    texture_handler_ = texture_manager::get_handler(new_texture_name);
    const std::unique_ptr<texture>& t_ptr = texture_manager::get_texture(texture_handler_);
    instance_data_.width = t_ptr->width;
    instance_data_.height = t_ptr->height;
}

void sprite::set_texture(const unsigned int &new_texture_handler) {
    texture_handler_ = new_texture_handler;
    const std::unique_ptr<texture>& t_ptr = texture_manager::get_texture(texture_handler_);
    instance_data_.width = t_ptr->width;
    instance_data_.height = t_ptr->height;
}

void sprite::load_uv_rect(const texture_atlas *atlas) {
    instance_data_.uv = atlas->get_uv(texture_handler_);
}

void sprite::markDead() {
    to_remove_ = true;
}

bool sprite::getDeadMark() const {
    return to_remove_;
}

const sprite::gpu_instance_data & sprite::get_gpu_instance_data() const {
    return instance_data_;
}

int sprite::getDepth() const {
    if (to_remove_)
        return std::numeric_limits<int>::max();
    return depth_;
}
