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

        GLFWwindow* window = glfwCreateWindow(2200, 1200, "Hello World!", nullptr, nullptr);
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

        size_t spaceshipTexture = res.loadTextureFile("spaceship_color", "res/textures/spaceship/color.png", "res/textures/spaceship/normal.png");

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
        // destruction::DestructionRailgun dest(40, 2, glm::vec2(-300, 0), 0);
        // ship.applyDestruction(dest);
        DestructibleObject ship = DestructibleObject(
            repo,
            res.texturesRepo[spaceshipTexture].object,
            atlas->get_uv(spaceshipTexture),
            {10, 10, 0, 1, {0, 0}});
        ship.apply_damage(glm::vec2(10, 0), 100.0f, 100.0f);

        // === Инициализация рендерера ===
        Renderer::init(window, &repo);

        // === Создание текстового блока ===
        // Text_box text(repo);
        // text.set_font(&font);
        // // Передаём u32string напрямую, т.к. localisation_manager больше не существует.
        // // Если нужна локализация — получайте строку из locale до передачи в set_text.
        // text.set_text(res.localesRepo["russian"].object["hello_world"]);
        // text.set_pos(-100, -300);
        // text.set_width(650);
        // text.bake_text();

        // for (int i = 0; i < 1; i += 1) {
        //     size_t spriteindex = repo.addSprite({}, 0);
        //     auto& spriteData = repo.dataFromSpriteIdx(spriteindex);
        //     spriteData.width = 115 * 10;
        //     spriteData.height = 60 * 10;
        //     spriteData.uvRect = repo.atlas.get_uv(spaceshipTexture);
        // }

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
            Renderer::renderAll(time);
            glfwSwapBuffers(window);
        }

        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }
}
