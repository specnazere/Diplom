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

import camera;
import shader;
import sprite;
import sprite_repository;
import resources;

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

    // VBO для инстанс-данных спрайтов (статические параметры + ID трансформации)
    static inline GLuint instance_vbo_ = 0;

    // SSBO для глобального массива трансформаций
    static inline GLuint transform_ssbo_ = 0;

    static inline SpritesRepository* repo_ = nullptr;

    static inline bool initialized_ = false;

public:
    static void init(GLFWwindow* window, SpritesRepository* repo)
    {
        if (initialized_) return;

        glfwWindowHint(GLFW_SAMPLES, 4); // 4x MSAA

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

            // 2. Инстанс-данные
            glGenBuffers(1, &instance_vbo_);
            glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);

            // Новая структура SpriteData (упрощенная для инстансинга, если мы вынесли позицию в TransformNode)
            // Но в текущем SpriteData из файла есть width, height, angle, pos, paralax, uv.
            // Мы будем использовать только width, height, paralax, uvRect и новый transformIdx.
            // Примечание: Если вы хотите полностью вынести позицию в иерархию,
            // то поля angle/pos в SpriteData могут игнорироваться шейдером или использоваться как смещение.
            // В данном коде мы предполагаем, что шейдер читает iTransformIdx.

            // Макет данных в VBO должен соответствовать тому, что мы отправляем.
            // Допустим, мы отправляем полный SpriteData, но шейдер интерпретирует его иначе.
            // Или лучше создать отдельную структуру для инстанс-буфера?
            // Для простоты оставим SpriteData, но добавим поле transformIdx в конец или используем существующее.

            // ВАЖНО: В предоставленном Sprite.ixx нет поля transformIdx в SpriteData.
            // Оно есть в классе Sprite. Значит, нам нужно сформировать отдельный буфер
            // или расширить SpriteData.
            // Давайте предположим, что мы добавили `uint transformIdx` в конец SpriteData
            // или используем отдельный вектор для инстанс-атрибутов.

            // Чтобы не ломать существующий SpriteData, давайте создадим отдельный буфер
            // только для индексов трансформаций, если их количество совпадает с количеством спрайтов.
            // Но проще всего модифицировать SpriteData.
            // Так как я не могу менять заголовочный файл здесь, я предположу,
            // что вы добавили `uint transformIdx` в struct SpriteData в файле sprite.ixx.

            // Если вы НЕ меняли SpriteData, то код ниже будет неверным.
            // Предположим, что SpriteData теперь выглядит так:
            // struct SpriteData { ...; uint transformIdx; };
            // И выравнивание учтено.

            // Атрибуты:
            // loc 2: width (float)
            // loc 3: height (float)
            // loc 4: transformIdx (uint) <-- ЗАМЕНА угла
            // loc 5: paralax (float) <-- Сдвиг, так как позиция удалена из атрибутов
            // loc 6: uvRect (vec4)

            // Проверка размеров:
            // Если в SpriteData нет transformIdx, этот код нужно адаптировать под вашу реальную структуру.
            // Ниже приведен код для структуры, где есть transformIdx.

            // Width
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, width));
            glEnableVertexAttribArray(2);
            glVertexAttribDivisor(2, 1);

            // Height
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, height));
            glEnableVertexAttribArray(3);
            glVertexAttribDivisor(3, 1);

            // Transform Index (UINT)
            // Внимание: используйте glVertexAttribIPointer для целочисленных типов!
            glVertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(SpriteData),
                (void*)offsetof(SpriteData, transformIdx));
            glEnableVertexAttribArray(4);
            glVertexAttribDivisor(4, 1);

            // Paralax
            glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, paralax));
            glEnableVertexAttribArray(5);
            glVertexAttribDivisor(5, 1);

            // UV Rect
            glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, uvRect));
            glEnableVertexAttribArray(6);
            glVertexAttribDivisor(6, 1);

            // iAngle (локальный) — location 4
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, angle));
            glEnableVertexAttribArray(4);
            glVertexAttribDivisor(4, 1);

            // iPos (локальный) — location 5
            glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, position_x));
            glEnableVertexAttribArray(5);
            glVertexAttribDivisor(5, 1);

            // iParalax — location 6
            glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, paralax));
            glEnableVertexAttribArray(6);
            glVertexAttribDivisor(6, 1);

            // iUVRect — location 7
            glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, uvRect));
            glEnableVertexAttribArray(7);
            glVertexAttribDivisor(7, 1);

            // iTransformIdx (UINT) — location 8
            glVertexAttribIPointer(8, 1, GL_UNSIGNED_INT, sizeof(SpriteData),
                (void*)offsetof(SpriteData, transformIdx));
            glEnableVertexAttribArray(8);
            glVertexAttribDivisor(8, 1);

            // iDepth — location 9
            glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteData),
                (void*)offsetof(SpriteData, depth));
            glEnableVertexAttribArray(9);
            glVertexAttribDivisor(9, 1);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }

        // === SSBO для трансформаций ===
        {
            glGenBuffers(1, &transform_ssbo_);
            // Привязываем к точке 0 (соответствует binding = 0 в шейдере)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, transform_ssbo_);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
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

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
        glEnable(GL_MULTISAMPLE);

        initialized_ = true;
    }

    static void resize(GLFWwindow* window)
    {
        glfwGetFramebufferSize(window, &window_width_, &window_height_);

        const int buffer_width  = window_width_ ;// + BUFFER_OVERSIZE;
        const int buffer_height = window_height_;// + BUFFER_OVERSIZE;

        camera::set_frame_size(buffer_width, buffer_height);

        if (fbo_)           glDeleteFramebuffers(1, &fbo_);
        if (color_texture_) glDeleteTextures(1, &color_texture_);
        if (rbo_)           glDeleteRenderbuffers(1, &rbo_);

        // Создаем текстуру цвета
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

    static void renderSprites(float time = 0)
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

        // Устанавливаем максимальную глубину обхода иерархии
        sprite_shader_->set_uniform("uMaxTransformDepth", 16);

        // 1. Обновляем SSBO с трансформациями
        // Данные должны быть в формате, ожидаемом шейдером: vec4(x, y, angle, parentIdx)
        // Метод getTransformsPointer() должен возвращать указатель на такой массив.
        const auto* transforms = repo_->getTransformsPointer();
        size_t transformCount = repo_->getTransformsCount();

        if (transforms && transformCount > 0) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, transform_ssbo_);
            // Важно: размер в байтах
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         transformCount * sizeof(TransformNode), // Убедитесь, что размер совпадает с vec4
                         transforms,
                         GL_DYNAMIC_DRAW);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        // 2. Обновляем инстанс-буфер со спрайтами
        glBindVertexArray(sprite_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);

        // Здесь предполагается, что getDataPointer возвращает обновленный массив SpriteData,
        // который теперь содержит поле transformIdx.
        glBufferData(GL_ARRAY_BUFFER,
                     repo_->getDataSize() * sizeof(SpriteData),
                     repo_->getDataPointer(),
                     GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // 3. Рендер
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6,
                              static_cast<GLsizei>(repo_->getDataSize()));
        glBindVertexArray(0);
#endif
    }

    static void prepareConsts()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_width_, window_height_);
        glClearColor(0.2f, 0.2f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    static void renderAll(const float& time)
    {
        prepareConsts();
        renderSprites(time);
    }

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