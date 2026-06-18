//
// Created by A on 02.02.2026.
//

module;

#include <GLFW/glfw3.h>
#include <cmath>

#include "glm/fwd.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/matrix_transform.hpp"

export module camera;

export
class camera
{
private:
    static inline float scale_ = 1;
    static inline float pos_x_ = 0;
    static inline float pos_y_ = 0;
    static inline float angle_ = 0; // в радианах

    static inline glm::mat4 view_ = 1;

    static inline int frame_width_ = 100;
    static inline int frame_height_ = 100;

    static void reload_view()
    {
        view_ = glm::ortho(
            frame_width_ * -scale_ / 2,
            frame_width_ * scale_ / 2,
            frame_height_ * -scale_ / 2,
            frame_height_ * scale_ / 2);

        // Смещение и поворот
        glm::mat4 transform = glm::translate(glm::mat4(1), glm::vec3(-pos_x_, -pos_y_, 0));

        view_ *= transform;
    }

public:
    [[nodiscard]] static const glm::mat4& get_view()
    {
        return view_;
    }

    static void reset_view()
    {
        scale_ = 1;
        pos_x_ = 0;
        pos_y_ = 0;
        angle_ = 0;
        reload_view();
    }

    static void add_pos(float x, float y)
    {
        pos_x_ += x;
        pos_y_ += y;
        reload_view();
    }

    static void set_pos(float x, float y)
    {
        pos_x_ = x;
        pos_y_ = y;
        reload_view();
    }

    static void mult_scale(float scale)
    {
        scale_ *= scale;
        reload_view();
    }

    static void set_scale(float scale)
    {
        scale_ = scale;
        reload_view();
    }

    static void set_frame_size(int width, int height)
    {
        frame_width_ = width;
        frame_height_ = height;
        reload_view();
    }

    [[nodiscard]] static float get_scale()
    {
        return scale_;
    }

    [[nodiscard]] static int get_frame_width()
    {
        return frame_width_;
    }

    [[nodiscard]] static int get_frame_height()
    {
        return frame_height_;
    }

    static glm::vec2 world_to_screen(const glm::vec2& world) {
        float half_w = frame_width_ * 0.5f;
        float half_h = frame_height_ * 0.5f;
        float rel_x = (world.x - pos_x_) / scale_;
        float rel_y = (world.y - pos_y_) / scale_;
        float screen_x = rel_x + half_w;
        float screen_y = -rel_y + half_h;
        return {screen_x, screen_y};
    }

    static glm::vec2 screen_to_world(const glm::vec2& screen) {
        float half_w = frame_width_ * 0.5f;
        float half_h = frame_height_ * 0.5f;
        float rel_x = screen.x - half_w;
        float rel_y = screen.y + half_h;
        float world_x = rel_x * scale_ + pos_x_;
        float world_y = rel_y * scale_ + pos_y_;
        return {world_x, world_y};
    }

    static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
        scale_ *= std::powf(0.8f, yoffset);
        reload_view();
    }
};