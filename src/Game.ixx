//
// Created by A on 01.02.2026.
//
module;

#include <memory>
#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include <stdexcept>
#include <string>
#include <set>
#include <vector>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

#include "graphics/uv_rect.h"

export module game;

import renderer;
import resources;
import sprite_repository;
import locale;
import font;
import atlas;
import destruction;

import destructible_entity;
import destructible_object;
import voxel_module;

import scene;
import player_object_controller;

// Сетевые модули
import native_tcp;
import resource_sync_protocol;
import host_resource_sync;
import client_resource_sync;
import scene_sync_protocol;
import host_scene_sync;
import client_scene_sync;

export namespace game {

GLFWwindow* window = nullptr;
Resources res = Resources();
std::unique_ptr<SpritesRepository> global_repo = nullptr;

enum class GameState { Menu, Scene, Exit };
enum class NetworkMode { Singleplayer, Host, Client };

// Глобальные сетевые объекты
NetworkMode current_network_mode = NetworkMode::Singleplayer;
std::unique_ptr<net::HostResourceSynchronizer> host_res_sync;
std::unique_ptr<net::ClientResourceSynchronizer> client_res_sync;
std::unique_ptr<net::HostSceneSynchronizer> host_scene_sync;
std::unique_ptr<net::ClientSceneSynchronizer> client_scene_sync;
bool has_client_connected = false;

// --- Helper: Init Systems ---
void init_systems() {
    if (window) return;

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(1600, 1200, "Network Game Test", nullptr, nullptr);
    if (!window) {
        const char* err_buffer = nullptr;
        glfwGetError(&err_buffer);
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window: " + std::string(err_buffer ? err_buffer : "Unknown error"));
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        const char* err_buffer = nullptr;
        glfwGetError(&err_buffer);
        glfwTerminate();
        throw std::runtime_error("Failed to load OpenGL: " + std::string(err_buffer ? err_buffer : "Unknown error"));
    }

    // Load Resources
    res.loadLocaleFile("res/localiz/placeholders.csv");
    res.loadFontFile("res/fonts/trafaretkit.ttf", "basic");
    res.loadTextureFile("spaceship", "res/textures/spaceship/color.png", "res/textures/spaceship/normal.png");
    res.loadTextureFile("turret", "res/textures/turret/color.png", "res/textures/turret/normal.png");

    std::set<char32_t> combined_charset;
    Locale& loc = res.localesRepo["russian"].object;
    const auto& cs = loc.getCharset();
    combined_charset.insert(cs.begin(), cs.end());
    res.initFontWithCharset(combined_charset, 150, "basic");

    Font& font = res.fontsRepo["basic"].object;
    Atlas* atlas = res.createAtlas();
    if (!atlas) throw std::runtime_error("Failed to create texture atlas");

    global_repo = std::make_unique<SpritesRepository>();
    global_repo->atlas = std::move(*atlas);

    Renderer::init(window, global_repo.get());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
}

// --- Helper: Create Scene Objects ---
// Возвращает указатель на DestructibleEntity для управления
std::vector<std::unique_ptr<DestructibleEntity>> create_scene_entities(Scene& scene) {
    std::vector<std::unique_ptr<DestructibleEntity>> entities;

    const auto& spaceship_texture = res.texturesRepo["spaceship"].object;
    const size_t spaceship_texture_id = res.texturesRepo["spaceship"].iDesc;
    const uv_rect spaceship_texture_uv = global_repo->atlas.get_uv(spaceship_texture_id);

    const auto& turret_texture = res.texturesRepo["turret"].object;
    const size_t turret_texture_id = res.texturesRepo["turret"].iDesc;
    const uv_rect turret_texture_uv = global_repo->atlas.get_uv(turret_texture_id);

    // Создаем первый корабль
    auto ship1 = std::make_unique<DestructibleEntity>(
        *global_repo,
        spaceship_texture, spaceship_texture_id, spaceship_texture_uv,
        DestructibleObject::Params{5.f, 5.f, 0.f, 1.f, {0.f, -200.f}} // Cell size 5, Origin y=-200
    );

    // 2. Двигатель (привязан к нижней части корабля)
    // Предположим, что двигатель занимает нижние 20% пикселей по высоте
    std::vector<std::pair<int, int>> engine_coords;
    int h_start = static_cast<int>(spaceship_texture.height * 0.8f);
    for (int x = 0; x < spaceship_texture.width; ++x) {
        for (int y = h_start; y < spaceship_texture.height; ++y) {
            engine_coords.emplace_back(x, y);
        }
    }

    std::vector<std::pair<int, int>> turret_coords;
    int turret_left = static_cast<int>(spaceship_texture.width * 0.5f) - 2;
    int turret_up = static_cast<int>(spaceship_texture.height * 0.5f) - 2;
    int turret_right = static_cast<int>(spaceship_texture.width * 0.5f) + 2;
    int turret_down = static_cast<int>(spaceship_texture.height * 0.5f) + 2;

    for (int x = turret_left; x < turret_right; ++x) {
        for (int y = turret_up; y < turret_down; ++y) {
            turret_coords.emplace_back(x, y);
        }
    }

    auto turret = std::make_unique<DestructibleObject>(
        *global_repo,
        turret_texture, turret_texture_id, turret_texture_uv,
        DestructibleObject::Params{5.f, 5.f, 0.1f, 1.f, {0.f, 0.f}});

    ship1->add_module(std::make_unique<VoxelModuleUniversalEngine>(10, glm::vec2(0.f), 0.f, engine_coords, 300000.f, 30000000.f));

    ship1->add_module(std::make_unique<VoxelModuleTurret>(
        15,
        glm::vec2(0.f),
        0.f,
        turret_coords,
        *turret.get(),
        10.f,
        ship1->get_object().get_local_transform()
        ));
    scene.add(std::move(turret), false);

    scene.add(std::unique_ptr<DestructibleObject>(&ship1->get_object())); // Scene должен хранить unique_ptr<DestructibleEntity> или базовый класс
    // Примечание: Если Scene хранит только DestructibleObject*, нужно будет изменить Scene или хранить Entity отдельно.
    // Для примера предположим, что мы храним Entity в отдельном списке или Scene адаптирован.
    // В текущей реализации Game мы будем хранить ссылки на Entity здесь, в векторе.

    entities.push_back(std::move(ship1));

    // Второй корабль (противник/статичный объект)
    // auto ship2 = std::make_unique<DestructibleEntity>(
    //     *global_repo,
    //     spaceship_texture, spaceship_texture_id, spaceship_texture_uv,
    //     DestructibleObject::Params{5.f, 5.f, 0.f, 1.f, {0.f, 200.f}}
    // );
    //
    // scene.add(std::unique_ptr<DestructibleObject>(&ship2->get_object()));
    // entities.push_back(std::move(ship2));

    return entities;
}

// --- Helper: Input Handling ---
InputState get_input() {
    InputState input{};
    input.up    = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    input.down  = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    input.left   = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    input.right = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    input.rotate_left  = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
    input.rotate_right = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;

    double x, y;
    glfwGetCursorPos(window, &x, &y);
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    x -= width / 2.f; y -= height / 2.f;
    input.globalCursorPosition = glm::vec2(x, -y);


    return input;
}

// --- State: Menu (ImGui) ---
GameState menu_state() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                           viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 300));

    ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

    ImGui::Text("Select Game Mode");
    ImGui::Separator();

    bool clicked = false;

    if (ImGui::Button("Singleplayer", ImVec2(-1, 50))) {
        current_network_mode = NetworkMode::Singleplayer;
        clicked = true;
    }

    ImGui::Spacing();

    if (ImGui::Button("Host Game", ImVec2(-1, 50))) {
        current_network_mode = NetworkMode::Host;
        clicked = true;
    }

    ImGui::Spacing();

    if (ImGui::Button("Join Game", ImVec2(-1, 50))) {
        current_network_mode = NetworkMode::Client;
        clicked = true;
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Exit", ImVec2(-1, 30))) {
        return GameState::Exit;
    }

    ImGui::End();

    ImGui::Render();

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
    glfwPollEvents();

    if (clicked) {
        return GameState::Scene;
    }

    if (glfwWindowShouldClose(window)) {
        return GameState::Exit;
    }

    return GameState::Menu;
}

// --- State: Singleplayer Loop ---
GameState run_singleplayer() {
    Scene scene;
    auto entities = create_scene_entities(scene);

    // Для примера управления берем первый корабль
    if (entities.empty()) return GameState::Menu;

    // PlayerController теперь должен работать с DestructibleObject&, который мы можем получить из Entity
    PlayerObjectController controller(*entities[0]);

    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float dt = static_cast<float>(current_time - last_time);
        last_time = current_time;

        scene.update(dt);

        // Управление игроком (изменяет velocity/angular_velocity внутри объекта)
        controller.update(get_input());

        // Рендеринг
        glfwPollEvents();
        Renderer::renderAll(0);

        // Отладочная отрисовка модулей (опционально)
        entities[0]->update(dt);
        //entities[1]->update(dt);

        glfwSwapBuffers(window);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            return GameState::Menu;
        }
    }
    return GameState::Exit;
}

// --- State: Host Loop ---
GameState run_host() {
    const unsigned short PORT = 9090;
    Scene scene;
    auto entities = create_scene_entities(scene);

    if (entities.empty()) return GameState::Menu;
    PlayerObjectController controller(*entities[0]);

    try {
        net::listen_start(PORT);
        std::cout << "Host: Listening on port " << PORT << "\n";

        std::cout << "Host: Waiting for connection...\n";
        if (!net::accept_connection(10000)) {
            std::cout << "Host: No client connected, returning to menu.\n";
            return GameState::Menu;
        }

        has_client_connected = true;
        std::cout << "Host: Client connected!\n";

        host_res_sync = std::make_unique<net::HostResourceSynchronizer>();
        host_scene_sync = std::make_unique<net::HostSceneSynchronizer>();

        host_res_sync->sync_repository(res.texturesRepo);

    } catch (const std::exception& e) {
        std::cerr << "Network init error: " << e.what() << "\n";
        return GameState::Menu;
    }

    double last_time = glfwGetTime();
    uint64_t tick_count = 0;

    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float dt = static_cast<float>(current_time - last_time);
        last_time = current_time;

        for (auto& entity : entities) {
            entity->update(dt);
        }

        controller.update(get_input());

        if (has_client_connected) {
            tick_count++;
            host_scene_sync->update(scene, tick_count);
        }

        glfwPollEvents();
        Renderer::renderAll(0);
        glfwSwapBuffers(window);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            return GameState::Menu;
        }
    }
    return GameState::Exit;
}

// --- State: Client Loop ---
GameState run_client() {
    const unsigned short PORT = 9090;
    Scene scene;

    try {
        std::cout << "Client: Connecting to 127.0.0.1:" << PORT << "\n";
        net::connect_to_host("127.0.0.1", PORT);
        std::cout << "Client: Connected!\n";

        has_client_connected = true;

        client_res_sync = std::make_unique<net::ClientResourceSynchronizer>(
            [&](const std::string& desc, uint32_t host_id) {
                if (res.texturesRepo.contains(desc)) {
                    std::cout << "Client: Mapped resource " << desc << "\n";
                }
            }
        );

        client_scene_sync = std::make_unique<net::ClientSceneSynchronizer>(scene, res, *global_repo);
        client_res_sync->load_resources_mapping();

    } catch (const std::exception& e) {
        std::cerr << "Network connection error: " << e.what() << "\n";
        return GameState::Menu;
    }

    double last_time = glfwGetTime();
    bool running = true;

    auto sync_thread = std::thread([&]() {
        while (running) {
            try {
                client_scene_sync->read_all();
            } catch (const std::exception& e) {
                std::cerr << "Network connection error: " << e.what() << "\n";
                break;
            }
        }
    });

    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float dt = static_cast<float>(current_time - last_time);
        last_time = current_time;

        // Клиент получает состояние из сети, но локальный update все же нужен для интерполяции или предсказания
        scene.update(dt);

        glfwPollEvents();
        Renderer::renderAll(0);
        glfwSwapBuffers(window);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            running = false;
            sync_thread.join();
            return GameState::Menu;
        }
    }

    running = false;
    sync_thread.join();

    return GameState::Exit;
}

// --- State: Scene Dispatcher ---
GameState scene_state() {
    has_client_connected = false;
    net::cleanup();
    host_res_sync.reset();
    client_res_sync.reset();
    host_scene_sync.reset();
    client_scene_sync.reset();

    switch (current_network_mode) {
        case NetworkMode::Host:
            return run_host();
        case NetworkMode::Client:
            return run_client();
        case NetworkMode::Singleplayer:
        default:
            return run_singleplayer();
    }
}

int run() {
    init_systems();
    GameState current_state = GameState::Menu;
    while (current_state != GameState::Exit) {
        switch (current_state) {
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
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
    return 0;
}

} // export namespace game