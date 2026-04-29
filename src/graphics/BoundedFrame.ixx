//
// NinePatch.ixx
//
// Класс окна интерфейса динамического размера на основе техники 9-slice (nine-patch).
//
// Текстура делится на 9 равных частей (3x3):
//
//   [TL] [TM] [TR]
//   [ML] [MM] [MR]
//   [BL] [BM] [BR]
//
//  TL/TR/BL/BR — углы:   не масштабируются
//  TM/BM       — верх/низ: масштабируются только по X
//  ML/MR       — бока:     масштабируются только по Y
//  MM          — центр:    масштабируется по X и Y
//
// Использование:
//   NinePatch win(repo, textureDesc, transformParentIdx);
//   win.set_geometry(x, y, width, height, depth);
//   win.resize(newWidth, newHeight);
//   win.move(newX, newY);
//

module;
#include "glm/glm.hpp"
#include "uv_rect.h"
#include <array>
#include <cassert>

export module nine_patch;

import sprite_repository;
import sprite;

// ─────────────────────────────────────────────────────────────────────────────
// Вспомогательная структура — индексы 9 слотов
// ─────────────────────────────────────────────────────────────────────────────

// Порядок слотов (строка за строкой, слева направо, сверху вниз):
//  0=TL  1=TM  2=TR
//  3=ML  4=MM  5=MR
//  6=BL  7=BM  8=BR
enum Slot : int {
    TL = 0, TM = 1, TR = 2,
    ML = 3, MM = 4, MR = 5,
    BL = 6, BM = 7, BR = 8,
    COUNT = 9
};

// Разбивает исходный uv_rect на 9 равных частей
std::array<uv_rect, 9> split_uv(const uv_rect& src)
{
    const float du = (src.u1 - src.u0) / 3.0f;
    const float dv = (src.v1 - src.v0) / 3.0f;

    std::array<uv_rect, 9> uvs{};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            uv_rect& r = uvs[row * 3 + col];
            r.u0 = src.u0 + col * du;
            r.v0 = src.v0 + row * dv;
            r.u1 = r.u0 + du;
            r.v1 = r.v0 + dv;
        }
    }

    return uvs;
}

// ─────────────────────────────────────────────────────────────────────────────
export
class BoundedFrame
{
public:
    // ─── Конструктор / Деструктор ────────────────────────────────────────────

    // repo           — репозиторий спрайтов
    // texture_desc   — хендл текстуры (передаётся в Sprite::textureDesc)
    // src_uv         — UV исходной текстуры в атласе (весь прямоугольник)
    // parent_transform — индекс родительской трансформации (-1 если нет)
    BoundedFrame(SpritesRepository& repo,
              size_t             texture_desc,
              int                parent_transform = -1)
        : repo_(repo)
        , texture_desc_(texture_desc)
    {
        // 1. Создаём общую трансформацию-якорь для всей группы
        root_transform_ = repo_.createTransform(parent_transform);

        // 2. Разбиваем UV на 9 частей
        tile_uvs_ = split_uv(repo_.atlas.get_uv(texture_desc));

        // 3. Создаём 9 спрайтов
        for (int i = 0; i < Slot::COUNT; ++i) {
            SpriteData sd{};
            sd.uvRect       = tile_uvs_[i];
            sd.transformIdx = static_cast<unsigned int>(root_transform_);

            sprite_indices_[i] = repo_.addSprite(sd, 0.0f, root_transform_);
        }
    }

    ~BoundedFrame()
    {
        // Удаляем все 9 спрайтов
        for (int i = 0; i < Slot::COUNT; ++i) {
            repo_.removeSprite(sprite_indices_[i]);
        }
        // Освобождаем корневую трансформацию
        repo_.destroyTransform(root_transform_);
    }

    // Запрет копирования — владеем ресурсами репозитория
    BoundedFrame(const BoundedFrame&)            = delete;
    BoundedFrame& operator=(const BoundedFrame&) = delete;

    // Перемещение разрешено
    BoundedFrame(BoundedFrame&&)            = default;
    BoundedFrame& operator=(BoundedFrame&&) = default;

    // ─── Основной интерфейс ──────────────────────────────────────────────────

    // Установить позицию, размер и глубину за один вызов
    void set_geometry(float x, float y,
                      float width, float height,
                      float depth = 0.0f)
    {
        depth_ = depth;
        repo_.setTransformLocal(root_transform_, x, y, 0.0f);
        apply_layout(width, height);
    }

    // Изменить только размер (позиция остаётся)
    void resize(float width, float height)
    {
        apply_layout(width, height);
    }

    // Изменить только позицию (размер остаётся)
    void move(float x, float y)
    {
        repo_.setTransformLocal(root_transform_, x, y, 0.0f);
    }

    // Текущий размер окна
    float width()  const { return width_;  }
    float height() const { return height_; }

    // Индекс корневой трансформации (для подвешивания дочерних элементов)
    size_t root_transform_index() const { return root_transform_; }

private:
    // ─── Вычисление раскладки ─────────────────────────────────────────────────
    //
    // Система координат: начало (0,0) — левый верхний угол окна.
    // position_x / position_y каждого спрайта — смещение центра спрайта
    // относительно origin трансформации (т.е. левого верхнего угла окна).
    //
    // Размеры угловых тайлов определяются как 1/3 от исходной текстуры.
    // Если окно меньше, чем два угловых тайла — минимальный размер клампируется.

    void apply_layout(float win_w, float win_h)
    {
        // Ширина/высота одного угла = 1/3 от исходного тайла.
        // Предполагаем, что SpriteData::width/height хранят пиксельный размер.
        // Запоминаем физический размер тайла из первого спрайта (или задаём явно).
        // Здесь используем corner_size_ — задаётся один раз при необходимости
        // (по умолчанию 1/3 от предположительного размера текстуры, обычно задаётся
        // через set_corner_size до первого вызова).

        const float cs = corner_size_; // размер угла (одинаков по X и Y)

        // Гарантируем минимальный размер окна
        win_w  = glm::max(win_w,  2.0f * cs);
        win_h  = glm::max(win_h,  2.0f * cs);

        width_  = win_w;
        height_ = win_h;

        // Размеры 9 тайлов:
        //  col: [cs]  [win_w - 2*cs]  [cs]
        //  row: [cs]  [win_h - 2*cs]  [cs]
        const float mid_w = win_w - 2.0f * cs;
        const float mid_h = win_h - 2.0f * cs;

        // Центры колонок (от левого края окна)
        const float cx[3] = {
            cs * -0.5f - mid_w * 0.5f,               // левая колонка
            0,       // средняя колонка
            cs *  0.5f + mid_w * 0.5f   // правая колонка
        };

        // Центры строк (от верхнего края окна)
        const float cy[3] = {
            cs * -0.5f - mid_h * 0.5f,               // верхняя строка
            0,       // средняя строка
            cs *  0.5f + mid_h * 0.5f   // нижняя строка
        };

        // Размеры тайлов по колонкам / строкам
        const float tw[3] = { cs, mid_w, cs };
        const float th[3] = { cs, mid_h, cs };

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const int slot = row * 3 + col;
                const size_t sprite_idx = sprite_indices_[slot];

                // Получаем указатель на SpriteData через dataFromSpriteIdx
                SpriteData& sd = repo_.dataFromSpriteIdx(sprite_idx);

                sd.width      = tw[col];
                sd.height     = th[row];
                sd.position_x = cx[col];
                sd.position_y = cy[row];
                sd.uvRect     = tile_uvs_[slot];
            }
        }

        // Обновляем глубину
        for (int i = 0; i < Slot::COUNT; ++i) {
            repo_[sprite_indices_[i]].depth = depth_;
        }
    }

    // ─── Поля ────────────────────────────────────────────────────────────────

    SpritesRepository& repo_;
    size_t             texture_desc_;

    size_t             root_transform_ = ~size_t{0};
    size_t             sprite_indices_[Slot::COUNT]{};

    std::array<uv_rect, 9> tile_uvs_{};

    float width_       = 0.0f;
    float height_      = 0.0f;
    float depth_       = 0.0f;

public:
    // Размер угловых тайлов в экранных единицах.
    // Должен соответствовать 1/3 физического размера исходной текстуры.
    // Устанавливайте до вызова set_geometry / resize.
    float corner_size_ = 32.0f;

    // Удобный сеттер
    void set_corner_size(float cs) { corner_size_ = cs; }
};
