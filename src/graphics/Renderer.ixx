//
// Created by A on 18.01.2026.
//

// === Global module fragment: все #include сторонних библиотек идут сюда ===
module;

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "primitives.h"

#include <cassert>
#include <stdexcept>
#include <vector>
#include <queue>

export module renderer;

export import camera;
export import shader;
export import sprite;
export import sprite_repository;
export import resources;
export import transform_node;

#define BUFFER_OVERSIZE 50
#define DEBUG_WIREFRAME

export
enum class render_layer
{
    background,
    main,
    sub_hud,
    hud,
    ui,
    count
};

export
class Renderer
{
private:
    static inline GLuint fbo_           = 0;
    static inline GLuint color_texture_ = 0;
    static inline GLuint rbo_           = 0;

    static inline Shader* sprite_shader_      = nullptr;
    static inline Shader* postprocess_shader_ = nullptr;

    static inline int window_width_  = 0;
    static inline int window_height_ = 0;

    static inline GLuint sprite_vao_   = 0;
    static inline GLuint quad_vao_     = 0;

    static inline GLuint instance_vbo_ = 0;
    static inline GLuint transform_ssbo_ = 0;

    static inline SpritesRepository* repo_ = nullptr;

    static inline bool initialized_ = false;

public:
    static void init(GLFWwindow* window, SpritesRepository* repo)
    {
        if (initialized_) return;

        glfwWindowHint(GLFW_SAMPLES, 4);

        repo_ = repo;

        glfwGetFramebufferSize(window, &window_width_, &window_height_);

        sprite_shader_      = new Shader("res/shaders/sprite");
        postprocess_shader_ = new Shader("res/shaders/postprocess");

        glfwSwapInterval(0);

        // === VAO для спрайтов (instancing) ===
        {
            glGenVertexArrays(1, &sprite_vao_);
            glBindVertexArray(sprite_vao_);

            // 1. Геометрия квада (локальная)
            GLuint vbo;
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            constexpr glm::float32 sprite_vertices[] = {
                -0.5f, -0.5f,
                 0.5f, -0.5f,
                 0.5f,  0.5f,
                -0.5f, -0.5f,
                 0.5f,  0.5f,
                -0.5f,  0.5f,
            };
            glBufferData(GL_ARRAY_BUFFER, sizeof(sprite_vertices), sprite_vertices, GL_STATIC_DRAW);

            // location 0: позиция вершины квада
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(glm::float32), (void*)0);
            glEnableVertexAttribArray(0);

            // инстанс-данные
            glGenBuffers(1, &instance_vbo_);
            glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);

            // width
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, width));
            glEnableVertexAttribArray(2);
            glVertexAttribDivisor(2, 1);

            // height
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, height));
            glEnableVertexAttribArray(3);
            glVertexAttribDivisor(3, 1);

            // angle
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, angle));
            glEnableVertexAttribArray(4);
            glVertexAttribDivisor(4, 1);

            // pos
            glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, position));
            glEnableVertexAttribArray(5);
            glVertexAttribDivisor(5, 1);

            // paralax
            glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, paralax));
            glEnableVertexAttribArray(6);
            glVertexAttribDivisor(6, 1);

            // uv rect
            glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, uvRect));
            glEnableVertexAttribArray(7);
            glVertexAttribDivisor(7, 1);

            // transform
            glVertexAttribIPointer(8, 1, GL_UNSIGNED_INT, sizeof(SpriteData),
                (void*)offsetof(SpriteData, transformIdx));
            glEnableVertexAttribArray(8);
            glVertexAttribDivisor(8, 1);

            // depth
            glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, depth));
            glEnableVertexAttribArray(9);
            glVertexAttribDivisor(9, 1);

            // color
            glVertexAttribPointer(10, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, color));
            glEnableVertexAttribArray(10);
            glVertexAttribDivisor(10, 1);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }

        // SSBO
        {
            glGenBuffers(1, &transform_ssbo_);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, transform_ssbo_);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        // VAO
        {
            glGenVertexArrays(1, &quad_vao_);
            glBindVertexArray(quad_vao_);

            GLuint vbo, ebo;
            glGenBuffers(1, &vbo);
            glGenBuffers(1, &ebo);

            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(textured_square_vertices),
                         textured_square_vertices, GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(square_indices),
                         square_indices, GL_STATIC_DRAW);

            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                  (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(1);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }

        resize(window);

        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int)
        {
            resize(w);
        });

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
        glEnable(GL_MULTISAMPLE);

        initialized_ = true;
    }

    static void resize(GLFWwindow* window)
    {
        glfwGetFramebufferSize(window, &window_width_, &window_height_);

        const int buffer_width  = window_width_ ;
        const int buffer_height = window_height_;

        camera::set_frame_size(buffer_width, buffer_height);

        if (fbo_)           glDeleteFramebuffers(1, &fbo_);
        if (color_texture_) glDeleteTextures(1, &color_texture_);
        if (rbo_)           glDeleteRenderbuffers(1, &rbo_);

        glGenTextures(1, &color_texture_);
        glBindTexture(GL_TEXTURE_2D, color_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, buffer_width, buffer_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenRenderbuffers(1, &rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, buffer_width, buffer_height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, color_texture_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, rbo_);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Framebuffer is not complete");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    static void render_sprites(float time = 0)
    {
        if (!initialized_ || !repo_) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);

#ifndef DONT_RENDER_SPRITES
        repo_->atlas.bind(sprite_shader_);

        sprite_shader_->use();
        sprite_shader_->set_uniform("uView", camera::get_view());
        sprite_shader_->set_uniform("uLightPos",
            glm::vec2(std::cos(time) * 500, std::sin(time) * 500));
        sprite_shader_->set_uniform("uMaxTransformDepth", 16);

        const auto* transforms = repo_->get_transforms_pointer();
        size_t transformCount = repo_->get_transforms_count();

        if (transforms && transformCount > 0) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, transform_ssbo_);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         transformCount * sizeof(TransformNode),
                         transforms,
                         GL_DYNAMIC_DRAW);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        glBindVertexArray(sprite_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);

        glBufferData(GL_ARRAY_BUFFER,
                     repo_->get_data_size() * sizeof(SpriteData),
                     repo_->get_data_pointer(),
                     GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArraysInstanced(GL_TRIANGLES, 0, 6,
                              static_cast<GLsizei>(repo_->get_data_size()));
        glBindVertexArray(0);
#endif
    }

    static void prepare_consts()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_width_, window_height_);
        glClearColor(0.2f, 0.2f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    static void render_all(const float& time)
    {
        prepare_consts();
        render_sprites(time);
    }

    static void remove_sprite(int sprite_idx)
    {
        assert(repo_ != nullptr);
        repo_->free_sprite(sprite_idx);
    }
};