//
// Created by A on 01.02.2026.
//

#include "Game.h"

#include <glad/glad.h>

#include "graphics/renderer.h"
#include "graphics/texture.h"
#include "graphics/texture_manager.h"
#include "graphics/text_box.h"

#include "misc/localisation_manager.h"

int game::run() {
    glfwInit();

    GLFWwindow* window = glfwCreateWindow(800, 600, "Hello World!", nullptr, nullptr);
    if (!window) {
        const char* err_buffer = nullptr;
        glfwGetError(&err_buffer);
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window: " + std::string(err_buffer));
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        const char* err_buffer = nullptr;
        glfwGetError(&err_buffer);
        glfwTerminate();
        throw std::runtime_error("Failed to load gll: " + std::string(err_buffer));
    }

    renderer::init(window);
    texture_manager::init();
    localisation_manager::init_localiz();

    double last_time = glfwGetTime();
    const double target_fps = 60.0;
    const double frame_time = 1.0 / target_fps;

    // sprite new_sprite("yoooo");

    texture_manager::reload_charset(localisation_manager::get_charset());
    font* new_font = texture_manager::load_font("res/fonts/trafaretkit.ttf", "basic", 50);

    texture_manager::create_atlas(new_font->get_handlers());

    new_font->set_atlas(texture_manager::atlas);
    text_box text;
    text.set_font(new_font);
    text.set_text("hello_world");
    text.set_pos(0, 150);
    text.set_width(650);

    texture_manager::atlas->bind();

    while (!glfwWindowShouldClose(window)) {
        const double current_time = glfwGetTime();
        const double delta = current_time - last_time;

        if (delta < frame_time) {
            glfwWaitEventsTimeout(frame_time - delta);
            continue;
        }

        last_time = current_time;

        glfwPollEvents();
        renderer::render();
        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
