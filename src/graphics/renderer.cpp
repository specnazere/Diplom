//
// Created by A on 18.01.2026.
//

#include <glad/glad.h>
#include <cassert>
#include <glm\glm.hpp>
#include <algorithm>

#include "primitives.h"

#include "shader.h"
#include "sprite.h"
#include "renderer.h"
#include "camera.h"
#include "texture_manager.h"

#define BUFFER_OVERSIZE 50

std::vector<sprite*> renderer::sprites_;
std::vector<sprite*> renderer::new_sprites_;

std::vector<sprite::gpu_instance_data> renderer::instance_data_;

std::mutex renderer::new_sprites_mutex_;

static GLuint fbo_ = 0;
static GLuint color_texture_ = 0;
static GLuint rbo_ = 0;
static shader* sprite_shader_ = nullptr;
static shader* postprocess_shader_ = nullptr;
static int window_width_ = 0;
static int window_height_ = 0;

static GLuint sprite_vao_ = 0;
static GLuint quad_vao_ = 0;

bool renderer::initialized_ = false;

void renderer::init(GLFWwindow* window) {
    if (initialized_) return;

    glfwGetFramebufferSize(window, &window_width_, &window_height_);

    sprite_shader_ = new shader("res/shaders/sprite");
    postprocess_shader_ = new shader("res/shaders/postprocess");

    // === Подготовка VAO для спрайтов (инстансинг) ===
    {
        glGenVertexArrays(1, &sprite_vao_);
        glBindVertexArray(sprite_vao_);

        GLuint vbo;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        // Используем 6 вершин (2 треугольника) для избежания индексного буфера в инстансинге
        constexpr glm::float32 sprite_vertices[] = {
            -0.5f, -0.5f,  // левый-низ
             0.5f, -0.5f,  // правый-низ
             0.5f,  0.5f,  // правый-верх

            -0.5f, -0.5f,  // левый-низ
             0.5f,  0.5f,  // правый-верх
            -0.5f,  0.5f,   // левый-верх
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(sprite_vertices), sprite_vertices, GL_STATIC_DRAW);

        // Атрибут позиции (location 0)
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(glm::float32), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    // === Подготовка VAO для полноэкранного квада (постпроцессинг) ===
    {
        glGenVertexArrays(1, &quad_vao_);
        glBindVertexArray(quad_vao_);

        GLuint vbo, ebo;
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(textured_square_vertices), textured_square_vertices, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(square_indices), square_indices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    // Инициализация ресайза и FBO
    resize(window);

    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        resize(w);
    });

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    initialized_ = true;
}

// Вызывается при инициализации и при изменении размера окна
void renderer::resize(GLFWwindow* window) {
    glfwGetFramebufferSize(window, &window_width_, &window_height_);

    const int buffer_width = window_width_ + BUFFER_OVERSIZE;
    const int buffer_height = window_height_ + BUFFER_OVERSIZE;

    camera::set_frame_size(buffer_width, buffer_height);

    // Освобождаем старые ресурсы при ресайзе
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (color_texture_) glDeleteTextures(1, &color_texture_);
    if (rbo_) glDeleteRenderbuffers(1, &rbo_);

    // Создаём цветовую текстуру
    glGenTextures(1, &color_texture_);
    glBindTexture(GL_TEXTURE_2D, color_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, buffer_width, buffer_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Framebuffer is not complete");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void renderer::render() {
    if (!initialized_) return;

    prepare_instances();
    if (instance_data_.empty()) return;

    // === Этап 1: Рендер спрайтов в FBO ===
    texture_manager::atlas->bind();

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, window_width_ + BUFFER_OVERSIZE, window_height_ + BUFFER_OVERSIZE);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    sprite_shader_->use();
    sprite_shader_->set_uniform("uView", camera::get_view());

    // Загрузка инстанс-данных
    static GLuint instance_vbo = 0;
    if (!instance_vbo) glGenBuffers(1, &instance_vbo);

    glBindVertexArray(sprite_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 instance_data_.size() * sizeof(sprite::gpu_instance_data),
                 instance_data_.data(),
                 GL_STREAM_DRAW);

    // Настройка атрибутов инстансинга
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(sprite::gpu_instance_data), nullptr); // width
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);

    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(sprite::gpu_instance_data), (void*)offsetof(sprite::gpu_instance_data, height));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);

    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(sprite::gpu_instance_data), (void*)offsetof(sprite::gpu_instance_data, angle));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(sprite::gpu_instance_data), (void*)offsetof(sprite::gpu_instance_data, position_x));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);

    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(sprite::gpu_instance_data), (void*)offsetof(sprite::gpu_instance_data, paralax));
    glEnableVertexAttribArray(6);
    glVertexAttribDivisor(6, 1);

    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(sprite::gpu_instance_data), (void*)offsetof(sprite::gpu_instance_data, uv));
    glEnableVertexAttribArray(7);
    glVertexAttribDivisor(7, 1);

    // Рендер инстансированных спрайтов
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(instance_data_.size()));

    // Очистка атрибутов инстансинга
    for (int i = 3; i <= 8; ++i) {
        glDisableVertexAttribArray(i);
    }
    glBindVertexArray(0);

    // === Этап 2: Постпроцессинг на экран ===
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_width_, window_height_);
    glClear(GL_COLOR_BUFFER_BIT);

    postprocess_shader_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_texture_);
    postprocess_shader_->set_uniform("screenTexture", 0);
    postprocess_shader_->set_uniform("resolution", glm::vec2(window_width_, window_height_));

    glBindVertexArray(quad_vao_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
}

void renderer::prepare_instances() {

    // Переносим новые спрайты в вектор спрайтов
    new_sprites_mutex_.lock(); {
        sprites_.append_range(new_sprites_);
        new_sprites_.clear();
    } new_sprites_mutex_.unlock();

    // Сортируем по глубине
    std::ranges::stable_sort(sprites_, [](const sprite* lhs, const sprite* rhs) {
        return lhs->getDepth() < rhs->getDepth();
    });

    // Удаляем "мёртвые"
    sprite* checkingSprite = sprites_[sprites_.size() - 1];
    while (checkingSprite->getDeadMark()) {
        sprites_.pop_back();
        delete checkingSprite;
        checkingSprite = sprites_[sprites_.size() - 1];
    }

    // Очищаем буфер с данными
    instance_data_.clear();
    instance_data_.reserve(sprites_.size());

    // Заносим данные
    for (auto* sprite : sprites_) {
        instance_data_.push_back(sprite->get_gpu_instance_data());
    }
}

void renderer::register_sprite(sprite *sprite) {
    new_sprites_.push_back(sprite);
}

void renderer::load_atlas(texture_atlas *atlas) {
    assert(initialized_);

    for (auto* sprite : new_sprites_) {
        sprite->load_uv_rect(atlas);
    }
}
