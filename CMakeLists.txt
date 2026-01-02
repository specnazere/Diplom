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

# Копирование папки при каждой сборке
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${RES_SOURCE_DIR}"
        "${RES_TARGET_DIR}"
        COMMENT "Copying resources to output directory"
)