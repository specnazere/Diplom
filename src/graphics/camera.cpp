//
// Created by A on 02.02.2026.
//

#include "camera.h"

float camera::scale_ = 1;
float camera::pos_x_ = 0;
float camera::pos_y_ = 0;

glm::mat4 camera::view_ = 0;

int camera::frame_width_ = 100;
int camera::frame_height_ = 100;


void camera::reload_view() {
    view_ = glm::ortho(
        frame_width_ * -scale_ / 2,
        frame_width_ * scale_ / 2,
        frame_height_ * -scale_ / 2,
        frame_height_ * scale_ / 2);

    view_[3][0] = pos_x_;
    view_[3][1] = pos_y_;
}

const glm::mat4 & camera::get_view() {
    return view_;
}

void camera::reset_view() {
    scale_ = 1;
    pos_x_ = 0; pos_y_ = 0;
    reload_view();
}

void camera::add_pos(const float &x, const float &y) {
    pos_x_ += x; pos_y_ += y;
    reload_view();
}

void camera::set_pos(const float &x, const float &y) {
    pos_x_ = x; pos_y_ = y;
    reload_view();
}

void camera::add_scale(const float &scale) {
    scale_ *= scale;
    reload_view();
}

void camera::set_scale(const float &scale) {
    scale_ = scale;
    reload_view();
}

void camera::set_frame_size(const int& width, const int& height) {
    frame_width_ = width; frame_height_ = height;
    reload_view();
}
