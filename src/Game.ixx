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

// Сетевые модули
import native_tcp;
// Импорт протоколов синхронизации (предполагается, что они обновлены под TCP или используют raw send/recv)
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
    // Настройка стиля, шрифтов и т.д.
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430"); // Или версия твоего шейдера
}

// --- Helper: Create Scene Objects ---
DestructibleObject* create_scene_objects(Scene& scene) {
    auto ship = std::make_unique<DestructibleObject>(
        *global_repo,
        res.texturesRepo["spaceship"].object, res.texturesRepo["spaceship"].iDesc,
        global_repo->atlas.get_uv(res.texturesRepo["spaceship"].iDesc),
        DestructibleObject::Params{5, 5, 0, 1, {0, -200}}
    );
    DestructibleObject* ship_ptr = ship.get();
    scene.add(std::move(ship));

    auto ship2 = std::make_unique<DestructibleObject>(
        *global_repo,
        res.texturesRepo["spaceship"].object, res.texturesRepo["spaceship"].iDesc,
        global_repo->atlas.get_uv(res.texturesRepo["spaceship"].iDesc),
        DestructibleObject::Params{5, 5, 0, 1, {0, 200}}
    );
    scene.add(std::move(ship2));

    return ship_ptr;
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
    return input;
}

    // --- State: Menu (ImGui) ---
    GameState menu_state() {
    // 1. Start ImGui Frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 2. Render Menu UI
    // Центрируем окно
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

    // 3. Render ImGui
    ImGui::Render();

    // Очищаем фон перед рендером ImGui (или оставляем свой clearColor, если хочешь видеть игру на фоне)
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
    DestructibleObject* player_ship = create_scene_objects(scene);
    PlayerController controller(*player_ship);
    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float dt = static_cast<float>(current_time - last_time);
        last_time = current_time;

        controller.update(dt, get_input());
        scene.update(dt);

        glfwPollEvents();
        Renderer::renderAll(0);
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
    DestructibleObject* player_ship = create_scene_objects(scene);
    PlayerController controller(*player_ship);

    try {
        net::listen_start(PORT);
        std::cout << "Host: Listening on port " << PORT << "\n";

        // Ожидаем подключения клиента
        std::cout << "Host: Waiting for connection...\n";
        if (!net::accept_connection(10000)) { // Таймаут 10 сек на подключение
             std::cout << "Host: No client connected, returning to menu.\n";
             return GameState::Menu;
        }

        has_client_connected = true;
        std::cout << "Host: Client connected!\n";

        // Инициализация синхронизаторов
        host_res_sync = std::make_unique<net::HostResourceSynchronizer>();

        host_scene_sync = std::make_unique<net::HostSceneSynchronizer>();

        // Синхронизируем ресурсы сразу после подключения
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

        // 1. Game Logic
        controller.update(dt, get_input());
        scene.update(dt);

        // 2. Network: Send Updates
        if (has_client_connected) {
            tick_count++;
            // Пример: отправка состояния сцены
            // host_scene_sync->update(scene, tick_count);
            // Примечание: реализация update зависит от твоего HostSceneSynchronizer
        }
        host_scene_sync->update(scene, tick_count);

        // 3. Render
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

        // Загрузка маппинга ресурсов
        client_res_sync->load_resources_mapping();

    } catch (const std::exception& e) {
        std::cerr << "Network connection error: " << e.what() << "\n";
        return GameState::Menu;
    }

    double last_time = glfwGetTime();
    bool running = true;

    auto sync_thread = std::thread([&]()
    {
        while (running) {
            try {
                client_scene_sync->read_all();
            }
            catch (const std::exception& e) {
                std::cerr << "Network connection error: " << e.what() << "\n";
                return GameState::Menu;
            }
        }
    });

    // sync_thread.detach();

    while (!glfwWindowShouldClose(window)) {
        double current_time = glfwGetTime();
        float dt = static_cast<float>(current_time - last_time);
        last_time = current_time;

        // 1. Network: Receive & Process
        // Здесь должен быть код приема данных.
        // Так как TCP - поток, нужно читать заголовки и диспетчеризировать.
        // Если твои синхронизаторы имеют метод process_network_input(), вызывай его здесь.
        // Например:
        // client_res_sync->process_pending_packets();
        // client_scene_sync->process_pending_packets();

        // Временная заглушка, если методы еще не готовы:
        // try {
        //    auto pkt = net::receive_typed<SomePacket>(10); // Блокирующий вызов с таймаутом
        // } catch (...) {}

        // 2. Client Logic
        // client_scene_sync->read_all();
        scene.update(dt);

        // 3. Render
        glfwPollEvents();
        Renderer::renderAll(0);
        glfwSwapBuffers(window);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            return GameState::Menu;
        }
    }
    running = false;
    sync_thread.join();

    return GameState::Exit;
}

// --- State: Scene Dispatcher ---
GameState scene_state() {
    // Reset network state
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