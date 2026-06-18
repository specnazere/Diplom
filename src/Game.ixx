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
#include <glm/ext/matrix_projection.hpp>

#include "actionGame/explosion_data.h"
#include "actionGame/physic_parameters.h"
#include "actionGame/voxel_object_params.h"
#include "graphics/uv_rect.h"

export module game;

import additional_math;

import renderer;
import resources;
import sprite_repository;
import locale;
import font;
import atlas;
import destruction;
import camera;

import moduled_entity;
import voxel_object;
import voxel_module;
import projectile;

import scene;
import player_object_controller;
import ai_object_controller;

import voxel_grid;

// Сетевые модули
import native_tcp;
import networked_voxel_object;
import client_receiver;
import client_sender;
import host_receiver;

export namespace game {

GLFWwindow* window = nullptr;
Resources res = Resources();
std::unique_ptr<SpritesRepository> global_repo = nullptr;

enum class GameState { Menu, Scene, Grid, Exit };
enum class NetworkMode { Singleplayer, Host, Client };

// Глобальные сетевые объекты
NetworkMode current_network_mode = NetworkMode::Singleplayer;
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
    res.addSolidColorTexture("blank", 255, 255, 255, 255);

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
    SpritesRepository::set_active(global_repo.get());

    Renderer::init(window, global_repo.get());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    glfwSetScrollCallback(window, camera::scroll_callback);
}

std::pair<AIObjectController, std::unique_ptr<ModuledEntity<VoxelObject>>> spawn_enemy(Scene& scene, const glm::vec2& pos)
{
    const auto& spaceship_texture = res.texturesRepo["spaceship"].object;
    const size_t spaceship_texture_id = res.texturesRepo["spaceship"].iDesc;
    const uv_rect spaceship_texture_uv = global_repo->atlas.get_uv(spaceship_texture_id);

    const auto& turret_texture = res.texturesRepo["turret"].object;
    const size_t turret_texture_id = res.texturesRepo["turret"].iDesc;
    const uv_rect turret_texture_uv = global_repo->atlas.get_uv(turret_texture_id);

    auto enemy = std::make_unique<ModuledEntity<VoxelObject>>(
        spaceship_texture, spaceship_texture_id, spaceship_texture_uv,
        voxel_object_params {5.f, 5.f, 0.f, 1.f, pos} // Cell size 5, Origin y=-200
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

    enemy->add_module(std::make_unique<VoxelModuleUniversalEngine>(10, glm::vec2(0.f), 0.f, engine_coords, 1000000.f, 100000000.f));

    ProjectileMakeupData proj{
        spaceship_texture_id,
        spaceship_texture_uv,
        kinetic_parameters (10.f, 100.f),
        glm::vec2(10.f, 10.f),
        explosion_data(25.f, 100.f, 10000.f),
        500.f,
        glm::vec2(150.f, 0.f),
        glm::vec2(1000.f, 0.f)
    };

    enemy->add_module(std::move(std::make_unique<VoxelModuleTurret>(
        15,
        glm::vec2(0.f),
        glm::vec2(-50.f, 0.f),
        0.f,
        turret_coords,
        global_repo->addSprite(SpriteData{
            turret_texture.width * 5.f,
            turret_texture.height * 5.f,
            0.f,
            {0.f, 0.f},
            1.f,
            0.1f,
            turret_texture_uv,
            {255, 255, 255, 255},
            ~0u
        }),
        10.f,
        enemy->get_object().get_grid_transform(),
        0.25f,
        proj
        )));

    scene.add(std::unique_ptr<VoxelObject>(&enemy->get_object())); // Scene должен хранить unique_ptr<DestructibleEntity> или базовый класс
    // Примечание: Если Scene хранит только DestructibleObject*, нужно будет изменить Scene или хранить Entity отдельно.
    // Для примера предположим, что мы храним Entity в отдельном списке или Scene адаптирован.
    // В текущей реализации Game мы будем хранить ссылки на Entity здесь, в векторе.

    return {AIObjectController(*enemy), std::move(enemy)};
}

// --- Helper: Create Scene Objects ---
// Возвращает указатель на DestructibleEntity для управления
std::unique_ptr<ModuledEntity<NetworkedVoxelObject>> create_player(Scene& scene)
{
    const auto& spaceship_texture = res.texturesRepo["spaceship"].object;
    const size_t spaceship_texture_id = res.texturesRepo["spaceship"].iDesc;
    const uv_rect spaceship_texture_uv = global_repo->atlas.get_uv(spaceship_texture_id);

    const auto& turret_texture = res.texturesRepo["turret"].object;
    const size_t turret_texture_id = res.texturesRepo["turret"].iDesc;
    const uv_rect turret_texture_uv = global_repo->atlas.get_uv(turret_texture_id);

    auto player = std::make_unique<ModuledEntity<NetworkedVoxelObject>>(
        std::make_unique<NetworkedVoxelObject>(
            0,
            spaceship_texture, spaceship_texture_id, "spaceship", spaceship_texture_uv,
            voxel_object_params{5.f, 5.f, 0.f, 1.f, {0.f, 0.f}})
    );

    // 2. Двигатель (привязан к нижней части корабля)
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

    player->add_module(std::make_unique<VoxelModuleUniversalEngine>(10, glm::vec2(0.f), 0.f, engine_coords, 1000000.f, 100000000.f));

    ProjectileMakeupData proj{
            spaceship_texture_id,
            spaceship_texture_uv,
            kinetic_parameters (10.f, 100.f),
            glm::vec2(10.f, 10.f),
            explosion_data(50.f, 100.f, 10000.f),
            500.f,
            glm::vec2(150.f, 0.f),
            glm::vec2(1000.f, 0.f)
        };

    player->add_module(std::move(std::make_unique<VoxelModuleTurret>(
        15,
        glm::vec2(0.f),
        glm::vec2(-50.f, 0.f),
        0.f,
        turret_coords,
        global_repo->addSprite(SpriteData{
            turret_texture.width * 5.f,
            turret_texture.height * 5.f,
            0.f,
            {0.f, 0.f},
            1.f,
            0.1f,
            turret_texture_uv,
            {255, 255, 255, 255},
            ~0u
        }),
        10.f,
        player->get_object().get_grid_transform(),
        0.25f,
        proj
        )));

    scene.add(std::unique_ptr<VoxelObject>(&player->get_object())); // Scene должен хранить unique_ptr<DestructibleEntity> или базовый класс
    // Примечание: Если Scene хранит только DestructibleObject*, нужно будет изменить Scene или хранить Entity отдельно.
    // Для примера предположим, что мы храним Entity в отдельном списке или Scene адаптирован.
    // В текущей реализации Game мы будем хранить ссылки на Entity здесь, в векторе.

    return std::move(player);
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
    input.fire = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    double x, y;
    glfwGetCursorPos(window, &x, &y);
    input.globalCursorPosition = camera::screen_to_world(glm::vec2(x, -y));

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

    GameState state = GameState::Menu;

    if (ImGui::Button("Singleplayer Action", ImVec2(-1, 50))) {
        current_network_mode = NetworkMode::Singleplayer;
        state = GameState::Scene;
        clicked = true;
    }

    ImGui::Spacing();

    if (ImGui::Button("Singleplayer Constructor", ImVec2(-1, 50))) {
        current_network_mode = NetworkMode::Singleplayer;
        state = GameState::Grid;
        clicked = true;
    }

    ImGui::Spacing();

    if (ImGui::Button("Host Game", ImVec2(-1, 50))) {
        current_network_mode = NetworkMode::Host;
        state = GameState::Scene;
        clicked = true;
    }

    ImGui::Spacing();

    if (ImGui::Button("Join Game", ImVec2(-1, 50))) {
        current_network_mode = NetworkMode::Client;
        state = GameState::Grid;
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
        return state;
    }

    if (glfwWindowShouldClose(window)) {
        return GameState::Exit;
    }

    return GameState::Menu;
}

GameState action_scene()
{
    Scene scene;
    auto player = create_player(scene);

    // PlayerController теперь должен работать с DestructibleObject&, который мы можем получить из Entity
    PlayerObjectController controller(*player);

    double last_time = glfwGetTime();

    const float enemy_cooldown = 10.f;
    float last_enemy_spawn = -2.f;
    float spawn_radius = 1000.f;

    std::vector<std::pair<AIObjectController, std::unique_ptr<ModuledEntity<VoxelObject>>>> enemies;

    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float dt = static_cast<float>(current_time - last_time);
        last_time = current_time;
        last_enemy_spawn += dt;

        while (last_enemy_spawn > 0.f) {
            enemies.push_back(spawn_enemy(scene, math::rotate_vec2({spawn_radius, 0.f}, rand() * 0.01f)));
            last_enemy_spawn -= enemy_cooldown;
        }

#ifdef DEBUG_COLLISION
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
#endif

        // Управление игроком (изменяет velocity/angular_velocity внутри объекта)
        controller.update(get_input());

        for (auto& enemy : enemies) {
            enemy.first.update(dt, player->get_object().global_position());
        }

        player->update(dt);

        for (auto& enemy : enemies) {
            enemy.second->update(dt);
        }

        scene.update(dt);

        glfwPollEvents();
        Renderer::renderAll(0);

#ifdef DEBUG_COLLISION
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif

        glfwSwapBuffers(window);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            return GameState::Menu;
        }
    }

    return GameState::Exit;
}

GameState grid_scene()
{
    double last_time = glfwGetTime();

    const auto& spaceship_texture = res.texturesRepo["spaceship"].object;
    const size_t blank_texture_id = res.texturesRepo["blank"].iDesc;
    const uv_rect blank_texture_uv = global_repo->atlas.get_uv(blank_texture_id);

    VoxelGrid grid(spaceship_texture, blank_texture_uv, 10.f);

    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float dt = static_cast<float>(current_time - last_time);
        last_time = current_time;

        grid.update(dt);
        grid.process_input(get_input());

        glfwPollEvents();
        Renderer::renderAll(0);

        glfwSwapBuffers(window);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            return GameState::Menu;
        }
    }

    return GameState::Exit;
}

// --- State: Singleplayer Loop ---
GameState run_singleplayer(GameState state) {
    switch (state) {
        case GameState::Scene:
            return action_scene();
        case GameState::Grid:
            return grid_scene();
        default:
            return GameState::Exit;
    }
}

// --- State: Host Loop ---
GameState run_host() {
    const unsigned short PORT = 9090;
    Scene scene;
    auto player = create_player(scene);

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

    } catch (const std::exception& e) {
        std::cerr << "Network init error: " << e.what() << "\n";
        return GameState::Menu;
    }

    player->get_object().send_full_snapshot();
    PlayerObjectController controller(*player);
    HostSender sender;

    double last_time = glfwGetTime();
    uint64_t tick_count = 0;

    const float enemy_cooldown = 10.f;
    float last_enemy_spawn = -2.f;
    float spawn_radius = 1000.f;

    std::vector<std::pair<AIObjectController, std::unique_ptr<ModuledEntity<VoxelObject>>>> enemies;

    auto heal_handler = [&player](const VoxelHealCommand& cmd) {
        player->get_object().apply_heal_command(cmd);
    };
    HostReceiver receiver(heal_handler);

    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float dt = static_cast<float>(current_time - last_time);
        last_time = current_time;
        last_enemy_spawn += dt;

        while (last_enemy_spawn > 0.f) {
            enemies.push_back(spawn_enemy(scene, math::rotate_vec2({spawn_radius, 0.f}, rand() * 0.01f)));
            last_enemy_spawn -= enemy_cooldown;
        }

#ifdef DEBUG_COLLISION
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
#endif

        controller.update(get_input());

        for (auto& enemy : enemies) {
            enemy.first.update(dt, player->get_object().global_position());
        }

        player->update(dt);

        for (auto& enemy : enemies) {
            enemy.second->update(dt);
        }

        scene.update(dt);

        if (has_client_connected) {
            receiver.process_commands();

            tick_count++;
            std::optional<VoxelBatchUpdate> batch = player->get_object().get_voxel_batch();
            if (batch.has_value()) {
                sender.add_batch(batch.value());
            }
            sender.TriggerWork();
        }

        glfwPollEvents();
        Renderer::renderAll(0);

#ifdef DEBUG_COLLISION
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
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

    std::unique_ptr<VoxelGrid> grid;
    const size_t blank_texture_id = res.texturesRepo["blank"].iDesc;
    const uv_rect blank_texture_uv = global_repo->atlas.get_uv(blank_texture_id);

    auto snapshot_handler = [&](uint32_t object_id, uint32_t cols, uint32_t rows,
                          std::string texture_name, const std::vector<VoxelStateUpdate>& updates) {
        grid = VoxelGrid::create_from_snapshot(res.texturesRepo[texture_name].object, blank_texture_uv, updates);
    };

    auto damage_handler = [&](const VoxelBatchUpdate& batch) {
        if (grid.get()) {
            grid->apply_network_update(batch);
        }
    };

    ClientReceiver receiver(snapshot_handler, damage_handler);

    try {
        std::cout << "Client: Connecting to 127.0.0.1:" << PORT << "\n";
        net::connect_to_host("127.0.0.1", PORT);
        std::cout << "Client: Connected!\n";

        has_client_connected = true;

    } catch (const std::exception& e) {
        std::cerr << "Network connection error: " << e.what() << "\n";
        return GameState::Menu;
    }

    ClientSender sender;

    double last_time = glfwGetTime();

    while (grid.get() == nullptr) {
        receiver.process_network_packets();
    }

    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float dt = static_cast<float>(current_time - last_time);
        last_time = current_time;

        receiver.process_network_packets();

        grid->update(dt);
        grid->process_input(get_input());

        glfwPollEvents();
        Renderer::renderAll(0);
        glfwSwapBuffers(window);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            return GameState::Menu;
        }
    }

    return GameState::Exit;
}

// --- State: Scene Dispatcher ---
GameState scene_state(GameState state) {
    has_client_connected = false;
    net::cleanup();

    switch (current_network_mode) {
        case NetworkMode::Host:
            return run_host();
        case NetworkMode::Client:
            return run_client();
        case NetworkMode::Singleplayer:
        default:
            return run_singleplayer(state);
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
        case GameState::Grid:
            current_state = scene_state(current_state);
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