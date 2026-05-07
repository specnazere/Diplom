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
    GLFWwindow* window = nullptr;
    Resources res = Resources();

    // Глобальный репозиторий спрайтов, если он нужен везде
    // Лучше инкапсулировать его внутри Renderer или Scene, но пока оставим так для совместимости с твоим кодом
    std::unique_ptr<SpritesRepository> global_repo = nullptr;

    enum class GameState
    {
        Menu,
        Scene,
        Exit
    };

    // --- Вспомогательная функция инициализации (вызывается один раз) ---
    void init_systems()
    {
        if (window) return; // Уже инициализировано

        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window = glfwCreateWindow(1600, 1200, "Hello World!", nullptr, nullptr);
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

        // Загрузка ресурсов
        res.loadLocaleFile("res/localiz/placeholders.csv");
        res.loadFontFile("res/fonts/trafaretkit.ttf", "basic");

        res.loadTextureFile("spaceship", "res/textures/spaceship/color.png", "res/textures/spaceship/normal.png");

        std::set<char32_t> combined_charset;
        Locale& loc = res.localesRepo["russian"].object;
        const auto& cs = loc.getCharset();
        combined_charset.insert(cs.begin(), cs.end());

        res.initFontWithCharset(combined_charset, 150, "basic");

        Font& font = res.fontsRepo["basic"].object;
        Atlas* atlas = res.createAtlas();
        if (!atlas)
            throw std::runtime_error("Failed to create texture atlas");

        // Инициализация репозитория спрайтов
        global_repo = std::make_unique<SpritesRepository>();
        global_repo->atlas = std::move(*atlas);

        // Инициализация рендерера
        Renderer::init(window, global_repo.get());
    }

    // --- Состояние Меню ---
    GameState menu_state()
    {
        // Здесь должен быть цикл меню
        // Пример упрощенной реализации:
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            // Очистка экрана (например, черным)
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // TODO: Отрисовка UI меню (ImGui или твои спрайты)
            // Если нажата кнопка "Start", возвращаем GameState::Scene

            // Для примера: нажми SPACE для перехода в игру
            if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
                return GameState::Scene;
            }

            // Нажми ESC для выхода
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                return GameState::Exit;
            }

            glfwSwapBuffers(window);
        }
        return GameState::Exit;
    }

    // --- Состояние Сцены ---
    GameState scene_state()
    {
        // Инициализация объектов сцены (происходит при входе в состояние)
        Scene scene;

        // ВАЖНО: Создаем объекты в куче
        auto ship = std::make_unique<DestructibleObject>(
            *global_repo,
            res.texturesRepo["spaceship"].object, // Используем дескриптор или ID корректно
            global_repo->atlas.get_uv(res.texturesRepo["spaceship"].iDesc), // Проверь API получения UV
            DestructibleObject::Params{5, 5, 0, 1, {0, -200}}
        );

        auto ship2 = std::make_unique<DestructibleObject>(
            *global_repo,
            res.texturesRepo["spaceship"].object,
            global_repo->atlas.get_uv(res.texturesRepo["spaceship"].iDesc),
            DestructibleObject::Params{5, 5, 0, 1, {0, 200}}
        );

        // Сохраняем сырые указатели для контроллера, но владение у Scene
        DestructibleObject* ship_ptr = ship.get();

        scene.add(std::move(ship));
        scene.add(std::move(ship2));

        PlayerController controller(*ship_ptr);

        double last_time = glfwGetTime();
        constexpr double target_fps = 60.0;
        constexpr double frame_time = 1.0 / target_fps;

        while (!glfwWindowShouldClose(window))
        {
            const double current_time = glfwGetTime();
            const double delta = current_time - last_time;

            // Ограничение FPS (опционально, раскомментируй если нужно)
            /*
            if (delta < frame_time) {
                glfwWaitEventsTimeout(frame_time - delta);
                continue;
            }
            */

            // Input
            InputState input;
            input.up    = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
            input.down  = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
            input.left  = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
            input.right = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
            input.rotate_left  = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
            input.rotate_right = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;

            // Update
            controller.update(delta, input);
            scene.update(delta);

            last_time = current_time;

            // Render
            glfwPollEvents();
            Renderer::renderAll(0); // Или передавай delta, если рендерер анимирует что-то
            glfwSwapBuffers(window);

            // Условие выхода из сцены обратно в меню
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                return GameState::Menu;
            }
        }
        return GameState::Exit;
    }

    int run()
    {
        init_systems();

        GameState current_state = GameState::Menu;

        while (current_state != GameState::Exit)
        {
            switch (current_state)
            {
                case GameState::Menu:
                    current_state = menu_state();
                    break;

                case GameState::Scene:
                    current_state = scene_state();
                    break;

                default:
                    current_state = GameState::Exit;
                    break;
            }
        }

        // Cleanup
        if (window) {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
        return 0;
    }
}