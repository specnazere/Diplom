cmake_minimum_required(VERSION 3.28) # Минимальная версия для хорошей поддержки модулей
project(Diplom LANGUAGES C CXX)

# Стандарт C++23 (как у вас было), модули работают и в 20, и в 23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Включаем автоматическое сканирование зависимостей модулей
# Это критически важно, чтобы CMake понял порядок компиляции .ixx файлов
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Найти зависимости
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)

# --- GLAD (остается обычной статической библиотекой, так как это C код) ---
add_library(glad STATIC)
set_target_properties(glad PROPERTIES LINKER_LANGUAGE C)
target_sources(glad PRIVATE thirdparty/glad/src/glad.c)
target_include_directories(glad PUBLIC thirdparty/glad/include)
target_link_libraries(glad PUBLIC glfw)

# --- Список исходных файлов модулей ---
# Мы убираем все .h файлы.
# Предполагается, что вы переименовали файлы реализации в .ixx (рекомендуется)
# или оставили .cpp, но внутри них написали 'export module ...'.
# Ниже приведен список с расширением .ixx для явного указания модулей.
# Если вы оставили .cpp, просто замените .ixx на .cpp в этом списке.

# Основное приложение
add_executable(${PROJECT_NAME} src/main.cpp)

# 1. Обычные источники (main.cpp, старые .cpp библиотеки)
# Они не являются модулями, поэтому добавляются как обычные источники
target_sources(${PROJECT_NAME} PRIVATE
        thirdparty/stb/stb_realisations.cpp
        thirdparty/tinyxml2/tinyxml2.cpp
)

# 2. Источники модулей (C++20 Modules)
# Ключевой момент: используем FILE_SET CXX_MODULES
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

        src/game/Spaceship.ixx
        src/game/Destruction.ixx
        src/game/DestructibleObject.ixx
        src/game/Scene.ixx
        src/game/PlayerController.ixx

        src/misc/Multitype_index_repository.ixx

        src/Game.ixx
)

# Подключение директорий
target_include_directories(${PROJECT_NAME} PRIVATE
        src
        thirdparty
)

# Линковка
target_link_libraries(${PROJECT_NAME} PRIVATE
        glad
        glm::glm
        OpenGL::GL
)
# Подключение директорий
# Важно: даже с модулями, если внутри :private используется #include <glm/...>,
# пути должны быть доступны компилятору.
target_include_directories(${PROJECT_NAME} PRIVATE
        src
        thirdparty
)

# Линковка
target_link_libraries(${PROJECT_NAME} PRIVATE
        glad
        glm::glm
        OpenGL::GL
)

# --- Ресурсы (логика копирования сохранена и немного улучшена) ---
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

# --- Дополнительные настройки для конкретных компиляторов (опционально) ---
if(MSVC)
    # MSVC иногда требует явного указания языка для .ixx
    set_source_files_properties(${MODULE_SOURCES} PROPERTIES LANGUAGE CXX)
    # Флаг для поддержки модулей в старых версиях VS 2022, в новых может не понадобиться
    target_compile_options(${PROJECT_NAME} PRIVATE /experimental:module)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Для GCC/Clang убеждаемся, что включен флаг сканирования (обычно включен через CMAKE_CXX_SCAN_FOR_MODULES)
    # Но иногда полезно добавить явно, если возникают проблемы с порядком компиляции
    target_compile_options(${PROJECT_NAME} PRIVATE -fmodules-ts)
endif()