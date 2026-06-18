cmake_minimum_required(VERSION 3.28)
project(Diplom LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)

add_library(glad STATIC)
set_target_properties(glad PROPERTIES LINKER_LANGUAGE C)
target_sources(glad PRIVATE thirdparty/glad/src/glad.c)
target_include_directories(glad PUBLIC thirdparty/glad/include)
target_link_libraries(glad PUBLIC glfw)

add_executable(${PROJECT_NAME} src/main.cpp)

# --- Чтение .defines ---
set(DEFINES_FILE "${CMAKE_SOURCE_DIR}/.defines")

if(EXISTS ${DEFINES_FILE})
    file(STRINGS ${DEFINES_FILE} DEFINES_LIST)
    set(COMPILED_DEFINES "")

    foreach(DEFINE IN LISTS DEFINES_LIST)
        # Убираем пробелы по краям
        string(STRIP "${DEFINE}" STRIPPED_DEFINE)

        # Пропускаем пустые строки и комментарии
        if(NOT STRIPPED_DEFINE STREQUAL "" AND NOT STRIPPED_DEFINE MATCHES "^#")
            # Берем только первое слово (разделитель - пробел)
            string(REGEX MATCH "^[^ ]+" FIRST_WORD "${STRIPPED_DEFINE}")

            if(FIRST_WORD)
                list(APPEND COMPILED_DEFINES ${FIRST_WORD})
            endif()
        endif()
    endforeach()

    if(COMPILED_DEFINES)
        target_compile_definitions(${PROJECT_NAME} PRIVATE ${COMPILED_DEFINES})
    endif()

    # Заставляет CMake перечитывать конфигурацию при изменении файла
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DEFINES_FILE})

    message(STATUS "Loaded definitions from .defines file: ${COMPILED_DEFINES}")
else()
    message(STATUS "No .defines file found, skipping custom definitions")
endif()

target_sources(${PROJECT_NAME} PRIVATE
        thirdparty/stb/stb_realisations.cpp
        thirdparty/tinyxml2/tinyxml2.cpp
        thirdparty/imgui/imgui.cpp
        thirdparty/imgui/imgui_draw.cpp
        thirdparty/imgui/imgui_tables.cpp
        thirdparty/imgui/imgui_widgets.cpp
        thirdparty/imgui/imgui_impl_opengl3.cpp
        thirdparty/imgui/imgui_impl_glfw.cpp
)

target_sources(${PROJECT_NAME} PRIVATE
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        src/graphics/Renderer.ixx
        src/graphics/Sprite.ixx
        src/graphics/Texture.ixx
        src/graphics/Atlas.ixx
        src/graphics/Shader.ixx
        src/graphics/Camera.ixx
        src/graphics/Font.ixx
        src/graphics/Resources.ixx
        src/graphics/Locale.ixx
        src/graphics/SpritesRepository.ixx
        src/graphics/TransformNode.ixx

        src/actionGame/DestructibleObject.ixx
        src/actionGame/Scene.ixx
        src/actionGame/PlayerObjectController.ixx
        src/actionGame/AIObjectController.ixx
        src/actionGame/ModuledEntity.ixx
        src/actionGame/VoxelModule.ixx
        src/actionGame/VoxelObject.ixx
        src/actionGame/Projectile.ixx
        src/actionGame/RecursiveHitbox.ixx
        src/actionGame/PhysicObject.ixx
        src/actionGame/SceneObject.ixx
        src/actionGame/NetworkedVoxelObject.ixx

        src/constructGame/VoxelGrid.ixx

        src/netcode/Native_tcp.ixx
        src/netcode/voxelHealthProtocol.ixx
        src/netcode/HostSender.ixx
        src/netcode/ClientReceiver.ixx
        src/netcode/HostReceiver.ixx
        src/netcode/ClientSender.ixx

        src/misc/Multitype_index_repository.ixx
        src/misc/AdditionalMath.ixx
        src/misc/InputState.ixx

        src/Game.ixx
)

target_include_directories(${PROJECT_NAME} PRIVATE
        src
        thirdparty
)

target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32
        glad
        glm::glm
        OpenGL::GL
)

target_compile_definitions(${PROJECT_NAME} PRIVATE _GLIBCXX_STDLIB_H=1)

set_source_files_properties(
        thirdparty/imgui/imgui.cpp
        thirdparty/imgui/imgui_draw.cpp
        thirdparty/imgui/imgui_tables.cpp
        thirdparty/imgui/imgui_widgets.cpp
        thirdparty/imgui/imgui_impl_opengl3.cpp
        thirdparty/imgui/imgui_impl_glfw.cpp
        thirdparty/stb/stb_realisations.cpp
        thirdparty/tinyxml2/tinyxml2.cpp
        PROPERTIES
        CXX_SCAN_FOR_MODULES OFF
        COMPILE_OPTIONS "-include;cstdlib"
)

set(RES_SOURCE_DIR "${CMAKE_SOURCE_DIR}/res")
set(RES_TARGET_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/res")

file(GLOB_RECURSE RES_FILES CONFIGURE_DEPENDS "${RES_SOURCE_DIR}/*")

set(RES_MARKER "${CMAKE_BINARY_DIR}/.res_copied")

add_custom_command(
        OUTPUT "${RES_MARKER}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${RES_SOURCE_DIR}" "${RES_TARGET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch "${RES_MARKER}"
        DEPENDS ${RES_FILES}
        COMMENT "Copying resources to ${RES_TARGET_DIR}"
        VERBATIM
)

add_custom_target(copy_resources ALL DEPENDS "${RES_MARKER}")
add_dependencies(${PROJECT_NAME} copy_resources)

if(MSVC)
    set_source_files_properties(${MODULE_SOURCES} PROPERTIES LANGUAGE CXX)
    target_compile_options(${PROJECT_NAME} PRIVATE /experimental:module)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
endif()