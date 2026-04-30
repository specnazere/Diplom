//
// BoundedFrame.ixx
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
// ─── Изменения относительно исходной версии (NinePatch.ixx) ─────────────────
//
//  1. Переименование: NinePatch → BoundedFrame, модуль nine_patch → nine_patch
//     (имя модуля сохранено для обратной совместимости).
//
//  2. UV больше не передаётся снаружи.
//     Было:   NinePatch(repo, texture_desc, src_uv, parent)
//     Стало:  BoundedFrame(repo, texture_desc, parent)
//     UV теперь запрашивается напрямую из атласа: repo_.atlas.get_uv(texture_desc).
//     Это убирает дублирование — у атласа уже есть маппинг хендл→UV.
//
//  3. Система координат тайлов изменилась с «от левого верхнего угла» на «от центра».
//     Было:   cx = { cs*0.5, cs + mid_w*0.5, cs + mid_w + cs*0.5 }  (всё положительно)
//     Стало:  cx = { -cs*0.5 - mid_w*0.5,  0,  cs*0.5 + mid_w*0.5 }
//     Таким образом origin трансформации (0,0) соответствует центру окна,
//     что согласуется с тем, как position_x/position_y интерпретируется в SpriteData
//     (смещение центра спрайта от origin его трансформации).
//
//  4. Enum Slot и функция split_uv вынесены из анонимного namespace в глобальный —
//     они были доступны только внутри translation unit, поведение не изменилось.
//
// ─────────────────────────────────────────────────────────────────────────────
//
// Использование:
//   BoundedFrame frame(repo, textureDesc);
//   frame.set_corner_size(32.0f);          // 1/3 физического размера текстуры
//   frame.set_geometry(x, y, w, h, depth); // позиция центра, размер, глубина
//   frame.resize(newW, newH);
//   frame.move(newX, newY);
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

// Разбивает uv_rect на 9 равных частей по сетке 3×3
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

    // repo             — репозиторий спрайтов
    // texture_desc     — хендл текстуры; UV берётся из repo.atlas
    // parent_transform — индекс родительской трансформации (-1 если нет)
    BoundedFrame(SpritesRepository& repo,
                 size_t             texture_desc,
                 int                parent_transform = -1)
        : repo_(repo)
        , texture_desc_(texture_desc)
    {
        // 1. Создаём общую трансформацию-якорь для всей группы
        root_transform_ = repo_.createTransform(parent_transform);

        // 2. UV запрашиваем из атласа по хендлу текстуры и делим на 9 частей
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
        for (int i = 0; i < Slot::COUNT; ++i)
            repo_.removeSprite(sprite_indices_[i]);
        repo_.destroyTransform(root_transform_);
    }

    BoundedFrame(const BoundedFrame&)            = delete;
    BoundedFrame& operator=(const BoundedFrame&) = delete;
    BoundedFrame(BoundedFrame&&)                 = default;
    BoundedFrame& operator=(BoundedFrame&&)      = default;

    // ─── Основной интерфейс ──────────────────────────────────────────────────

    // Установить позицию центра окна, размер и глубину за один вызов
    void set_geometry(float x, float y,
                      float width, float height,
                      float depth = 0.0f)
    {
        depth_ = depth;
        repo_.setTransformLocal(root_transform_, x, y, 0.0f);
        apply_layout(width, height);
    }

    // Изменить только размер (позиция остаётся прежней)
    void resize(float width, float height)
    {
        apply_layout(width, height);
    }

    // Изменить только позицию центра (размер остаётся прежним)
    void move(float x, float y)
    {
        repo_.setTransformLocal(root_transform_, x, y, 0.0f);
    }

    float width()  const { return width_;  }
    float height() const { return height_; }

    // Индекс корневой трансформации — используется для привязки дочерних элементов
    size_t root_transform_index() const { return root_transform_; }

    // Размер угловых тайлов в экранных единицах (= 1/3 физического размера текстуры).
    // Устанавливать до первого вызова set_geometry / resize.
    float corner_size_ = 32.0f;
    void set_corner_size(float cs) { corner_size_ = cs; }

private:
    // ─── Раскладка ───────────────────────────────────────────────────────────
    //
    // Система координат: (0,0) — центр окна (origin трансформации).
    // position_x/y каждого спрайта — смещение его центра от origin.
    //
    //  Колонки по X:  [-mid_w/2 - cs/2]  [0]  [+mid_w/2 + cs/2]
    //  Строки по Y:   [-mid_h/2 - cs/2]  [0]  [+mid_h/2 + cs/2]

    void apply_layout(float win_w, float win_h)
    {
        const float cs = corner_size_;

        win_w = glm::max(win_w, 2.0f * cs);
        win_h = glm::max(win_h, 2.0f * cs);

        width_  = win_w;
        height_ = win_h;

        const float mid_w = win_w - 2.0f * cs;
        const float mid_h = win_h - 2.0f * cs;

        // Центры колонок относительно центра окна
        const float cx[3] = {
            -cs * 0.5f - mid_w * 0.5f, // левая
             0.0f,                     // средняя
             cs * 0.5f + mid_w * 0.5f  // правая
        };

        // Центры строк относительно центра окна
        const float cy[3] = {
            -cs * 0.5f - mid_h * 0.5f, // верхняя
             0.0f,                     // средняя
             cs * 0.5f + mid_h * 0.5f  // нижняя
        };

        const float tw[3] = { cs, mid_w, cs };
        const float th[3] = { cs, mid_h, cs };

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const int    slot = row * 3 + col;
                SpriteData&  sd   = repo_.dataFromSpriteIdx(sprite_indices_[slot]);

                sd.width      = tw[col];
                sd.height     = th[row];
                sd.position_x = cx[col];
                sd.position_y = cy[row];
                sd.uvRect     = tile_uvs_[slot];
            }
        }

        for (int i = 0; i < Slot::COUNT; ++i)
            repo_.dataFromSpriteIdx(sprite_indices_[i]).depth = depth_;
    }

    // ─── Поля ────────────────────────────────────────────────────────────────

    SpritesRepository&     repo_;
    size_t                 texture_desc_;

    size_t                 root_transform_ = ~size_t{0};
    size_t                 sprite_indices_[Slot::COUNT]{};
    std::array<uv_rect, 9> tile_uvs_{};

    float width_  = 0.0f;
    float height_ = 0.0f;
    float depth_  = 0.0f;
};
