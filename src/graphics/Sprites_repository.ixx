//
// Created by A on 17.04.2026.
//

module;
#include "glm/glm.hpp"

#include <vector>
#include <queue>

export module sprite_repository;

import atlas;
import sprite;

export
class SpritesRepository
{
private:
    // Объекты спрайтов. Никогда не сжимаются — свободные слоты переиспользуются.
    std::vector<Sprite>   sprites_;

    // Плотный массив данных для GPU. Меняется только при добавлении/удалении.
    std::vector<SpriteData> sprite_data_;

    // Очередь свободных индексов в векторе sprites_ (для переиспользования).
    std::queue<int> free_sprite_indices_;

public:
    Atlas atlas;

    // Добавление спрайта. Возвращает индекс в векторе sprites_.
    size_t addSprite(const SpriteData& data, float depth)
    {
        // 1. Добавляем данные в плотный массив
        const size_t new_data_idx = sprite_data_.size();
        sprite_data_.push_back(data);

        // 2. Получаем (или создаём) слот в векторе sprites_
        size_t sprite_idx;
        if (!free_sprite_indices_.empty()) {
            sprite_idx = static_cast<size_t>(free_sprite_indices_.front());
            free_sprite_indices_.pop();
            sprites_[sprite_idx] = Sprite{};
        } else {
            sprite_idx = sprites_.size();
            sprites_.push_back(Sprite{});
        }

        Sprite& s  = sprites_[sprite_idx];
        s.data     = new_data_idx;
        s.depth    = depth;
        s.active   = true;

        return sprite_idx;
    }

    // Удаление спрайта по индексу в векторе sprites_.
    // Использует swap-and-pop для поддержания плотности данных.
    void removeSprite(int sprite_idx)
    {
        if (sprite_idx < 0 || sprite_idx >= static_cast<int>(sprites_.size())) return;

        Sprite& spr = sprites_[sprite_idx];
        if (!spr.active) return;

        const size_t data_idx_to_remove = spr.data;
        const size_t last_data_idx      = sprite_data_.size() - 1;

        // Swap-and-pop: последний элемент переезжает на место удаляемого
        if (data_idx_to_remove != last_data_idx) {
            sprite_data_[data_idx_to_remove] = sprite_data_[last_data_idx];

            // Обновляем ссылку на данные у спрайта, который ссылался на последний слот
            for (Sprite& s : sprites_) {
                if (s.active && s.data == last_data_idx) {
                    s.data = data_idx_to_remove;
                    break;
                }
            }
        }

        sprite_data_.pop_back();

        // Помечаем спрайт как неактивный и возвращаем слот в очередь
        spr.active = false;
        spr.data   = ~size_t{0};
        free_sprite_indices_.push(sprite_idx);
    }

    // --- Доступ к данным для GPU ---

    const SpriteData* getDataPointer() const
    {
        return sprite_data_.empty() ? nullptr : sprite_data_.data();
    }

    size_t getDataSize() const { return sprite_data_.size(); }

    // --- Прямой доступ ---

    SpriteData& dataFromSpriteIdx(size_t index)
    {
        return sprite_data_[sprites_[index].data];
    }

    Sprite& operator[](size_t index) { return sprites_[index]; }
};
