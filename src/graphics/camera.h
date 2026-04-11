//
// Created by A on 02.02.2026.
//

#ifndef CAMERA_H
#define CAMERA_H



#include <GLFW/glfw3.h>
#include <glm/fwd.hpp>
#include <glm/gtc/type_ptr.hpp>

class camera {
private:
    static float scale_;
    static float pos_x_;
    static float pos_y_;

    static glm::mat4 view_;

    static int frame_width_;
    static int frame_height_;

    static void reload_view();
public:
    [[nodiscard]] static const glm::mat4& get_view();

    static void reset_view();

    static void add_pos(const float &x, const float &y);
    static void set_pos(const float &x, const float &y);
    static void add_scale(const float &scale);
    static void set_scale(const float &scale);

    static void set_frame_size(const int &width, const int &height);
};



#endif //CAMERA_H
