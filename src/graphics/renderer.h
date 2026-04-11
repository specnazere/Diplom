//
// Created by A on 18.01.2026.
//

#ifndef RENDERER_H
#define RENDERER_H



#include <vector>
#include <mutex>
#include <GLFW/glfw3.h>

#include "sprite.h"

enum class render_layer {
    background,
    main,
    sub_hud,
    hud,
    ui,
    count  // для итерации
};

class renderer {
public:
    static void init(GLFWwindow* window);

    static void resize(GLFWwindow* window);

    static void render();

    static void register_sprite(sprite* sprite);
    static void load_atlas(texture_atlas* atlas);

private:
    static std::vector<sprite*> sprites_;
    static std::vector<sprite*> new_sprites_;

    static std::vector<sprite::gpu_instance_data> instance_data_;

    static bool initialized_;

    static std::mutex new_sprites_mutex_;

    static void prepare_instances();
};



#endif //RENDERER_H
