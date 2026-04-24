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

export module renderer;

import camera;
import shader;
import sprite;
import sprite_repository;
import resources;
import mesh;

#define BUFFER_OVERSIZE 50
//#define DEBUG_WIREFRAME

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
    static inline Shader* mesh_shader_        = nullptr;
    static inline Shader* postprocess_shader_ = nullptr;
#ifdef DEBUG_WIREFRAME
    static inline Shader* wireframe_shader_   = nullptr;
#endif

    static inline int window_width_  = 0;
    static inline int window_height_ = 0;

    static inline GLuint sprite_vao_   = 0;
    static inline GLuint quad_vao_     = 0;
    static inline GLuint instance_vbo_ = 0;

    static inline SpritesRepository* repo_ = nullptr;
    static inline std::vector<Mesh*>* meshes_;

    static inline bool initialized_ = false;

public:
    static void init(GLFWwindow* window, SpritesRepository* repo, std::vector<Mesh*>* meshes)
    {
        if (initialized_) return;

        repo_ = repo;
        meshes_ = meshes;

        glfwGetFramebufferSize(window, &window_width_, &window_height_);

        sprite_shader_      = new Shader("res/shaders/sprite");
        mesh_shader_        = new Shader("res/shaders/mesh");
        postprocess_shader_ = new Shader("res/shaders/postprocess");
#ifdef DEBUG_WIREFRAME
        wireframe_shader_ = new Shader("res/shaders/flatBlack");
#endif

        glfwSwapInterval(0);

        // === VAO для спрайтов (instancing) ===
        {
            glGenVertexArrays(1, &sprite_vao_);
            glBindVertexArray(sprite_vao_);

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

            glGenBuffers(1, &instance_vbo_);
            glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);

            // SpriteData layout:
            //   offset  0: width      (float) → loc 2
            //   offset  4: height     (float) → loc 3
            //   offset  8: angle      (float) → loc 4
            //   offset 12: position_x (float) → loc 5 (vec2 с position_y)
            //   offset 16: position_y (float)
            //   offset 20: paralax    (float) → loc 6
            //   offset 24: uvRect     (vec4)  → loc 7

            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, width));
            glEnableVertexAttribArray(2);
            glVertexAttribDivisor(2, 1);

            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, height));
            glEnableVertexAttribArray(3);
            glVertexAttribDivisor(3, 1);

            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, angle));
            glEnableVertexAttribArray(4);
            glVertexAttribDivisor(4, 1);

            glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, position_x));
            glEnableVertexAttribArray(5);
            glVertexAttribDivisor(5, 1);

            glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, paralax));
            glEnableVertexAttribArray(6);
            glVertexAttribDivisor(6, 1);

            glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, uvRect));
            glEnableVertexAttribArray(7);
            glVertexAttribDivisor(7, 1);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }

        // === VAO для полноэкранного квада (постпроцессинг) ===
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

        // Depth test включён — аппаратная сортировка по глубине для спрайтов и мешей.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);

        initialized_ = true;
    }

    static void resize(GLFWwindow* window)
    {
        glfwGetFramebufferSize(window, &window_width_, &window_height_);

        const int buffer_width  = window_width_  + BUFFER_OVERSIZE;
        const int buffer_height = window_height_ + BUFFER_OVERSIZE;

        camera::set_frame_size(buffer_width, buffer_height);

        if (fbo_)           glDeleteFramebuffers(1, &fbo_);
        if (color_texture_) glDeleteTextures(1, &color_texture_);
        if (rbo_)           glDeleteRenderbuffers(1, &rbo_);

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

    // Рендер всех спрайтов из репозитория.
    // Сортировка на CPU убрана — глубина обрабатывается depth test'ом.
    static void renderSprites(float time = 0)
    {
        if (!initialized_ || !repo_) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);

        repo_->atlas.bind(sprite_shader_);

        sprite_shader_->use();
        sprite_shader_->set_uniform("uView", camera::get_view());
        sprite_shader_->set_uniform("uLightPos",
            glm::vec2(std::cos(time) * 500, std::sin(time) * 500));

        glBindVertexArray(sprite_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     repo_->getDataSize() * sizeof(SpriteData),
                     repo_->getDataPointer(),
                     GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArraysInstanced(GL_TRIANGLES, 0, 6,
                              static_cast<GLsizei>(repo_->getDataSize()));
        glBindVertexArray(0);
    }

    // Рендер одного меша.
    // Если меш помечен как dirty — автоматически загружает данные на GPU перед рисованием.
    // Шейдер "res/shaders/mesh" получает:
    //   layout(location=0) vec2 aPosition
    //   layout(location=1) vec2 aUV
    //   uniform mat4  uView
    //   uniform float uDepth
    static void renderMeshes()
    {
        if (!initialized_) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);

        repo_->atlas.bind(mesh_shader_);

        for (Mesh* mesh : *meshes_) {
            if (mesh->is_dirty()) mesh->upload();

            mesh_shader_->use();
            mesh_shader_->set_uniform("uView",  camera::get_view());
            mesh_shader_->set_uniform("uLightPos", glm::vec2(500, 500));
            mesh_shader_->set_uniform("iParalax", 1.0f);
            mesh_shader_->set_uniform("iPos", glm::vec2(0, 0));
            mesh_shader_->set_uniform("iAngle", 0.0f);
            mesh_shader_->set_uniform("iDepth", 0.5f);

            glBindVertexArray(mesh->vao());
            glDrawElements(GL_TRIANGLES, mesh->index_count(), GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);

#ifdef DEBUG_WIREFRAME
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glLineWidth(1.5f);

            wireframe_shader_->use();
            wireframe_shader_->set_uniform("uView", camera::get_view());
            glBindVertexArray(mesh->vao());
            glDrawElements(GL_TRIANGLES, mesh->index_count(), GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);

            glEnable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
        }
    }

    static void prepareConsts()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_width_, window_height_);
        glClearColor(0.2f, 0.2f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    static void renderAll()
    {
        prepareConsts();
        renderSprites();
        renderMeshes();
    }

    // Добавление спрайта через репозиторий.
    static void register_sprite(const SpriteData& data, float depth, size_t texture_desc)
    {
        assert(repo_ != nullptr && "Renderer not initialized with a SpritesRepository");
        repo_->addSprite(data, depth);
    }

    static void remove_sprite(int sprite_idx)
    {
        assert(repo_ != nullptr);
        repo_->removeSprite(sprite_idx);
    }
};
