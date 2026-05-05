//
// Created by A on 01.02.2026.
//

// === Global module fragment: сторонние #include идут сюда, до объявления модуля ===
module;

#include <memory>

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
import atlas;
import destruction;
import destructible_object;
import scene;
import player_controller;

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
        res.loadFontFile("res/fonts/trafaretkit.ttf", "basic");

        size_t shipTexture = res.loadTextureFile("spaceship", "res/textures/spaceship/color.png", "res/textures/spaceship/normal.png");

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
        res.initFontWithCharset(combined_charset, 150, "basic");

        // Получаем ссылку на шрифт и связываем его с атласом
        Font& font = res.fontsRepo["basic"].object;

        // Создаём GPU-атлас из текстуры шрифта
        Atlas* atlas = res.createAtlas();
        if (!atlas)
            throw std::runtime_error("Failed to create texture atlas");

        // === Инициализация репозитория спрайтов ===
        // Перемещаем атлас в репозиторий (он там хранится по значению)
        SpritesRepository repo;
        repo.atlas = std::move(*atlas);

        //win.resize(600, 400);
        //win.move(150, 250);

        // === Инициализация рендерера ===
        Renderer::init(window, &repo);

        // PhysicsScene scene;
        // auto obj1 = scene.add(
        //     PhysicsObject(
        //         DestructibleObject(
        //             repo,
        //             res.texturesRepo[shipTexture].object,
        //             atlas->get_uv(shipTexture),
        //             DestructibleObject::Params(
        //                 1,
        //                 10,
        //                 0,
        //                 1,
        //                 glm::vec2(
        //                     0.0f,
        //                     100.0f
        //                 )
        //             )
        //         ),
        //         PhysicsObject::Params(
        //             1,
        //             15,
        //             1,
        //             0.1f
        //         )
        //     )
        // );
        // auto obj2 = scene.add(
        //     PhysicsObject(
        //         DestructibleObject(
        //             repo,
        //             res.texturesRepo[shipTexture].object,
        //             atlas->get_uv(shipTexture),
        //             DestructibleObject::Params(
        //                 1,
        //                 10,
        //                 0,
        //                 1,
        //                 glm::vec2(
        //                     0.0f,
        //                     -200.0f
        //                 )
        //             )
        //         ),
        //         PhysicsObject::Params(
        //             1,
        //             15,
        //             1,
        //             0.1f
        //         )
        //     )
        // );
        //
        // obj1->apply_impulse({0, -100}, {0, 100});

        auto scene = Scene();
        auto ship = DestructibleObject(repo, res.texturesRepo[shipTexture].object, atlas->get_uv(shipTexture), {5, 5, 0, 1, {0, -200}});
        auto ship2 = DestructibleObject(repo, res.texturesRepo[shipTexture].object, atlas->get_uv(shipTexture), {5, 5, 0, 1, {0, 200}});
        scene.add(std::unique_ptr<DestructibleObject>(&ship));
        scene.add(std::unique_ptr<DestructibleObject>(&ship2));

        PlayerController controller(ship);

        // === Игровой цикл ===
        double last_time = glfwGetTime();
        constexpr double target_fps  = 60.0;
        constexpr double frame_time  = 1.0 / target_fps;

        while (!glfwWindowShouldClose(window))
        {
            const double current_time = glfwGetTime();
            const double delta        = current_time - last_time;

            // if (delta < frame_time) {
            //     glfwWaitEventsTimeout(frame_time - delta);
            //     continue;
            // }
            //
            InputState input;
            input.up    = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
            input.down  = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
            input.left  = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
            input.right = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
            input.rotate_left  = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
            input.rotate_right = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;

            controller.update(delta, input);

            scene.update(delta);

            last_time = current_time;
            glfwPollEvents();
            Renderer::renderAll(0);
            glfwSwapBuffers(window);
        }

        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }
}
