cmake_minimum_required(VERSION 3.31)
project(Diplom LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Найти зависимости
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# GLAD: предполагаем, что он лежит в поддиректории glad/
add_library(glad STATIC)
set_target_properties(glad PROPERTIES LINKER_LANGUAGE C)
target_sources(glad PRIVATE thirdparty/glad/src/glad.c)
target_include_directories(glad PUBLIC thirdparty/glad/include)
target_link_libraries(glad PUBLIC glfw)

# Основное приложение
add_executable(${PROJECT_NAME} src/main.cpp
        src/main.cpp
        src/graphics/renderer.cpp
        src/graphics/renderer.h
        src/graphics/sprite.cpp
        src/graphics/sprite.h
        src/graphics/texture_manager.cpp
        src/graphics/texture_manager.h
        src/graphics/texture.cpp
        src/graphics/texture.h
        src/graphics/primitives.h
        src/graphics/texture_manager.cpp
        src/graphics/texture_manager.h
        src/graphics/texture_atlas.cpp
        src/graphics/texture_atlas.h
        src/graphics/shader.cpp
        src/graphics/shader.h
        src/game.cpp
        src/game.h
        thirdparty/stb/stb_realisations.cpp
        thirdparty/tinyxml2/tinyxml2.cpp
        src/graphics/camera.cpp
        src/graphics/camera.h
        src/graphics/text_box.cpp
        src/graphics/text_box.h
        src/graphics/font.cpp
        src/graphics/font.cpp
        src/graphics/font.h
        src/misc/localisation_manager.cpp
        src/misc/localisation_manager.h
        src/graphics/graphics_pipeliner.cpp
        src/graphics/graphics_pipeliner.h

)

# Заголовки
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} PRIVATE thirdparty)
# Зависимости
target_link_libraries(${PROJECT_NAME} PRIVATE glad glm::glm)

# OpenGL (на большинстве систем подключается автоматически через GLFW)
find_package(OpenGL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)

# Определяем пути
set(RES_SOURCE_DIR "${CMAKE_SOURCE_DIR}/res")
set(RES_TARGET_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/res")

# Собираем файлы ресурсов с отслеживанием изменений
file(GLOB_RECURSE RES_FILES
        CONFIGURE_DEPENDS
        "${RES_SOURCE_DIR}/*"
)

# Маркер в директории сборки (абсолютный путь, без генераторных выражений)
set(RES_MARKER "${CMAKE_BINARY_DIR}/.res_copied")

# Копируем в правильную директорию (генераторное выражение допустимо в COMMAND)
add_custom_command(
        OUTPUT "${RES_MARKER}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${RES_SOURCE_DIR}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/res"
        COMMAND ${CMAKE_COMMAND} -E touch "${RES_MARKER}"
        DEPENDS ${RES_FILES}
        COMMENT "Copying resources to $<TARGET_FILE_DIR:${PROJECT_NAME}>/res"
        VERBATIM
)

add_custom_target(copy_resources ALL
        DEPENDS "${RES_MARKER}"
)

add_dependencies(${PROJECT_NAME} copy_resources)