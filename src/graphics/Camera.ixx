//
// Created by A on 02.02.2026.
//

module;

#include "glm/fwd.hpp"
#include "glm/gtc/type_ptr.hpp"

export module camera;

export
class camera
{
private:
    static inline float scale_ = 1;
    static inline float pos_x_ = 0;
    static inline float pos_y_ = 0;

    static inline glm::mat4 view_ = 0;

    static inline int frame_width_ = 100;
    static inline int frame_height_ = 100;

    static void reload_view()
    {
        view_ = glm::ortho(
            frame_width_ * -scale_ / 2,
            frame_width_ * scale_ / 2,
            frame_height_ * -scale_ / 2,
            frame_height_ * scale_ / 2);

        view_[3][0] = pos_x_;
        view_[3][1] = pos_y_;
    }

public:
    static const glm::mat4& get_view()
    {
        return view_;
    }

    static void reset_view()
    {
        scale_ = 1;
        pos_x_ = 0;
        pos_y_ = 0;
        reload_view();
    }

    static void add_pos(const float& x, const float& y)
    {
        pos_x_ += x;
        pos_y_ += y;
        reload_view();
    }

    static void set_pos(const float& x, const float& y)
    {
        pos_x_ = x;
        pos_y_ = y;
        reload_view();
    }

    static void add_scale(const float& scale)
    {
        scale_ *= scale;
        reload_view();
    }

    static void set_scale(const float& scale)
    {
        scale_ = scale;
        reload_view();
    }

    static void set_frame_size(const int& width, const int& height)
    {
        frame_width_ = width;
        frame_height_ = height;
        reload_view();
    }
};
