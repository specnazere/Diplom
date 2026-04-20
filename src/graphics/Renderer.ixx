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
#include <algorithm>
#include <stdexcept>

export module renderer;

import camera;
import shader;
import sprite;
import sprite_repository;
import resources;

#define BUFFER_OVERSIZE 50

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

    static inline shader* sprite_shader_      = nullptr;
    static inline shader* postprocess_shader_ = nullptr;

    static inline int window_width_  = 0;
    static inline int window_height_ = 0;

    static inline GLuint sprite_vao_ = 0;
    static inline GLuint quad_vao_   = 0;
    static inline GLuint instance_vbo_ = 0;  // Вынесен в статику — не пересоздаём каждый кадр

    // Репозиторий спрайтов (владение не наше, только указатель)
    static inline SpritesRepository* repo_ = nullptr;

    // Буфер для передачи отсортированных данных в GPU
    //static inline std::vector<SpriteData> instance_data_;

    static inline bool initialized_ = false;

    // Собирает отсортированные SpriteData из репозитория
    // static void prepare_instances()
    // {
    //     if (!repo_) return;
    //
    //     // Сортируем индексы по глубине внутри репозитория
    //     repo_->sort();
    //
    //     const SpriteData* raw_data   = repo_->getDataPointer();
    //     const size_t*     indices    = repo_->getIndicesPointer();
    //     const size_t      count      = repo_->getIndicesSize();
    //
    //     if (!raw_data || !indices) return;
    //
    //     // Копируем данные в порядке сортировки
    //     instance_data_.clear();
    //     instance_data_.reserve(count);
    //     for (size_t i = 0; i < count; ++i)
    //         instance_data_.push_back(raw_data[indices[i]]);
    // }

public:
    // Инициализация. repo — репозиторий спрайтов (должен жить дольше Renderer-а)
    static void init(GLFWwindow* window, SpritesRepository* repo)
    {
        if (initialized_) return;

        repo_ = repo;

        glfwGetFramebufferSize(window, &window_width_, &window_height_);

        sprite_shader_      = new shader("res/shaders/sprite");
        postprocess_shader_ = new shader("res/shaders/postprocess");

        glfwSwapInterval(0);

        // === VAO для спрайтов (instancing) ===
        {
            glGenVertexArrays(1, &sprite_vao_);
            glBindVertexArray(sprite_vao_);

            // Квад из 6 вершин (2 треугольника) — без EBO, для совместимости с instancing
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

            // Instance VBO — данные пересоздаются каждый кадр
            glGenBuffers(1, &instance_vbo_);
            glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);

            // Настраиваем атрибуты инстансинга заранее (один раз в VAO)
            // Структура SpriteData (alignas(16)):
            //   offset  0: width       (float)
            //   offset  4: height      (float)
            //   offset  8: angle       (float)
            //   offset 12: position_x  (float)
            //   offset 16: position_y  (float)
            //   offset 20: paralax     (float)
            //   offset 24: uvRect      (uv_rect = 4 float = u0,v0,u1,v1)

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

            // position_x и position_y идут подряд → передаём как vec2
            glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, position_x));
            glEnableVertexAttribArray(5);
            glVertexAttribDivisor(5, 1);

            glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, paralax));
            glEnableVertexAttribArray(6);
            glVertexAttribDivisor(6, 1);

            // uv_rect {u0,v0,u1,v1} → vec4
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

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);

        initialized_ = true;
    }

    static void resize(GLFWwindow* window)
    {
        glfwGetFramebufferSize(window, &window_width_, &window_height_);

        const int buffer_width  = window_width_  + BUFFER_OVERSIZE;
        const int buffer_height = window_height_ + BUFFER_OVERSIZE;

        camera::set_frame_size(buffer_width, buffer_height);

        // Освобождаем старые ресурсы при ресайзе
        if (fbo_)           glDeleteFramebuffers(1, &fbo_);
        if (color_texture_) glDeleteTextures(1, &color_texture_);
        if (rbo_)           glDeleteRenderbuffers(1, &rbo_);

        // Создаём цветовую текстуру
        glGenTextures(1, &color_texture_);
        glBindTexture(GL_TEXTURE_2D, color_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, buffer_width, buffer_height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Создаём depth/stencil RBO
        glGenRenderbuffers(1, &rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, buffer_width, buffer_height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        // Создаём FBO
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

    static void render()
    {
        if (!initialized_ || !repo_) return;

        // prepare_instances();

        // === Этап 1: Рендер спрайтов в FBO ===

        // Биндим атлас из репозитория
        repo_->atlas.bind();

        // glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        // glViewport(0, 0, window_width_ + BUFFER_OVERSIZE, window_height_ + BUFFER_OVERSIZE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_width_, window_height_);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        sprite_shader_->use();
        sprite_shader_->set_uniform("uView", camera::get_view());

        // Обновляем instance VBO текущими данными (STREAM — данные меняются каждый кадр)
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

        // // === Этап 2: Постпроцессинг на экран ===
        // glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // glViewport(0, 0, window_width_, window_height_);
        // glClear(GL_COLOR_BUFFER_BIT);
        //
        // postprocess_shader_->use();
        // glActiveTexture(GL_TEXTURE0);
        // glBindTexture(GL_TEXTURE_2D, color_texture_);
        // postprocess_shader_->set_uniform("screenTexture", 0);
        // postprocess_shader_->set_uniform("resolution",
        //                                  glm::vec2(window_width_, window_height_));
        //
        // glBindVertexArray(quad_vao_);
        // glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        // glBindVertexArray(0);
        //
        // glDisable(GL_BLEND);
    }

    // Добавление спрайта через репозиторий.
    // Возвращает индекс в SpritesRepository для последующего управления.
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
