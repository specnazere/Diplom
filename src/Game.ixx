//
// Created by A on 01.02.2026.
//

// === Global module fragment: сторонние #include идут сюда, до объявления модуля ===
module;

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"

#include <stdexcept>
#include <string>
#include <set>
#include <vector>

export module game;

import renderer;
import resources;
import sprite_repository;
import locale;
import font;
import text_box;
import atlas;
import destruction;
import destructible_object;
import nine_patch;

// Вызывается из main.cpp как game::run()
export
namespace game
{
    int run()
    {
        // === Инициализация GLFW ===
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        GLFWwindow* window = glfwCreateWindow(1600, 1200, "Hello World!", nullptr, nullptr);
        if (!window) {
            const char* err_buffer = nullptr;
            glfwGetError(&err_buffer);
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window: " + std::string(err_buffer));
        }

        glfwMakeContextCurrent(window);

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            const char* err_buffer = nullptr;
            glfwGetError(&err_buffer);
            glfwTerminate();
            throw std::runtime_error("Failed to load OpenGL: " + std::string(err_buffer));
        }

        // === Инициализация ресурсов ===
        Resources res;

        // Загружаем таблицу локализации
        res.loadLocaleFile("res/localiz/placeholders.csv");

        // Загружаем файл шрифта (данные TTF, без растеризации)
        res.loadFontFile("res/fonts/trafaretkit.ttf", "basic", 50.0f);

        size_t frame = res.loadTextureFile("frame", "res/textures/interface/basic frame.png");

        // Получаем набор символов из первой (или нужной) локали
        // Предполагаем, что loadLocaleFile добавил локаль "ru" (или нужную)
        // Charset строим из всех имеющихся локалей
        std::set<char32_t> combined_charset;
        {
            // Добавляем основной charset из загруженной локали.
            // Предполагаем ключ "ru" — скорректируйте под реальный дескриптор в CSV.
            Locale& loc = res.localesRepo["russian"].object;
            const auto& cs = loc.getCharset();
            combined_charset.insert(cs.begin(), cs.end());
        }

        // Растеризуем глифы шрифта под полученный charset
        res.initFontWithCharset(combined_charset, 50, "basic");

        // Получаем ссылку на шрифт и связываем его с атласом
        Font& font = res.fontsRepo["basic"].object;
        font.bakeGlyphs(combined_charset, res.texturesRepo);

        // Создаём GPU-атлас из текстуры шрифта
        Atlas* atlas = res.createAtlas();
        if (!atlas)
            throw std::runtime_error("Failed to create texture atlas");

        // === Инициализация репозитория спрайтов ===
        // Перемещаем атлас в репозиторий (он там хранится по значению)
        SpritesRepository repo;
        repo.atlas = std::move(*atlas);

        BoundedFrame win(repo, frame);
        win.set_corner_size(50.0f); // 1/3 от высоты/ширины исходной текстуры в пикселях
        win.set_geometry(0, 0, 200, 100, 0.5f); // x, y, w, h, depth

        //win.resize(600, 400);
        // win.move(150, 250);

        // === Инициализация рендерера ===
        Renderer::init(window, &repo);

        // === Игровой цикл ===
        double last_time = glfwGetTime();
        constexpr double target_fps  = 60.0;
        constexpr double frame_time  = 1.0 / target_fps;
        float time = 0;

        while (!glfwWindowShouldClose(window))
        {
            time += 0.006;
            // const double current_time = glfwGetTime();
            // const double delta        = current_time - last_time;
            //
            // if (delta < frame_time) {
            //     glfwWaitEventsTimeout(frame_time - delta);
            //     continue;
            // }
            //
            // last_time = current_time;

            glfwPollEvents();
            Renderer::renderAll(0);
            glfwSwapBuffers(window);
        }

        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }
}
