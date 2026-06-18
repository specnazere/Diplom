//
// Created by A on 17.04.2026.
//
module;

#include "glm/glm.hpp"

#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>

export module sprite_repository;

export import atlas;
export import sprite;
export import transform_node;

export
class SpritesRepository
{
private:
    // Разреженный массив объектов-оберток. Индексы стабильны.
    std::vector<Sprite> sprites_;

    // Плотный массив данных для рендеринга (позиции, UV и т.д.).
    // Важно: если вы используете инстансинг с отдельным буфером трансформаций,
    // этот массив может содержать только статические данные (размеры, UV).
    // Но в текущей реализации он содержит полные данные.
    std::vector<SpriteData> sprite_data_;

    // Массив узлов трансформации. Индексы стабильны.
    std::vector<TransformNode> transforms_;

    // Очередь свободных индексов для спрайтов
    std::queue<size_t> free_sprite_indices_;

    // Очередь свободных индексов для трансформаций
    std::queue<size_t> free_transform_indices_;

public:
    Atlas atlas;

    // --- Управление Трансформациями ---

    // Создает новый узел трансформации.
    // Возвращает индекс в массиве transforms_.
    size_t createTransform(int parentIndex = -1) {
        size_t idx;
        if (!free_transform_indices_.empty()) {
            idx = free_transform_indices_.front();
            free_transform_indices_.pop();
            // Сброс данных не обязателен, если мы их перезаписываем,
            // но для чистоты можно обнулить.
            transforms_[idx] = TransformNode{};
        } else {
            idx = transforms_.size();
            transforms_.push_back(TransformNode{});
        }

        transforms_[idx].parentIndex = parentIndex;
        return idx;
    }

    // Освобождает узел трансформации.
    // Внимание: если есть спрайты, ссылающиеся на этот индекс, они станут "висячими".
    // Нужно убедиться, что спрайты, использующие эту трансформацию, тоже удалены или перепривязаны.
    void destroyTransform(size_t transformIdx) {
        if (transformIdx >= transforms_.size()) return;

        // Опционально: сбросить родителя у детей? Нет, это дорого.
        // Просто возвращаем индекс в пул.
        free_transform_indices_.push(transformIdx);

        // Помечаем как невалидный (например, родителем -2 или специальным флагом, если нужно)
        transforms_[transformIdx].parentIndex = -2; // Маркер "удалено"
    }

    void setTransformLocal(size_t transformIdx, float x, float y, float angle) {
        if (transformIdx >= transforms_.size()) return;
        transforms_[transformIdx].position.x = x;
        transforms_[transformIdx].position.y = y;
        transforms_[transformIdx].angle = angle;
    }

    // Вычисляет глобальную трансформацию для конкретного узла (для CPU-логики)
    // Возвращает {x, y, angle} в мировых координатах
    glm::vec3 computeGlobalTransform(size_t transformIdx) const {
        if (transformIdx >= transforms_.size()) return {0,0,0};

        // Сбор цепочки родителей
        std::vector<const TransformNode*> path;
        size_t currentIdx = transformIdx;
        int safety = 0;

        while (currentIdx < transforms_.size() && transforms_[currentIdx].parentIndex >= 0 && safety < 100) {
            path.push_back(&transforms_[currentIdx]);
            currentIdx = static_cast<size_t>(transforms_[currentIdx].parentIndex);
        }

        // Если последний элемент тоже имеет валидного родителя (корень), добавляем его
        if (currentIdx < transforms_.size() && transforms_[currentIdx].parentIndex == -1) {
             path.push_back(&transforms_[currentIdx]);
        }

        // Вычисление от корня к листу
        float globalX = 0;
        float globalY = 0;
        float globalAngle = 0;

        // Идем с конца вектора (корень) к началу (текущий узел)
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            const TransformNode* node = *it;

            // Поворачиваем текущие накопленные координаты на угол этого узла?
            // Нет. Логика:
            // Глобальная позиция ребенка = Глобальная позиция родителя + (Локальный вектор ребенка, повернутый на Глобальный угол родителя)

            // 1. Сначала обновляем глобальный угол
            globalAngle += node->angle;

            // 2. Поворачиваем локальное смещение этого узла на НАКОПЛЕННЫЙ УГОЛ ПРЕДЫДУЩИХ (родительских) узлов
            // Но так как мы идем от корня, то для корня родительский угол = 0.
            // Для следующего узла родительский угол = угол корня.

            // Чтобы это работало итеративно, нам нужно хранить "родительский угол" перед добавлением текущего.
            // Давайте перепишем цикл чуть иначе для ясности:
        }

        // Пересчет правильным методом (накопление матрицы или вектора)
        globalX = 0; globalY = 0; globalAngle = 0;

        // Нам нужно идти от корня к листу.
        // Вектор path сейчас: [Лист, ..., Корень] (так как мы пушили от листа вверх)
        // Значит, rbegin() - это Корень.

        float currentAccumulatedAngle = 0;
        float currentAccumulatedX = 0;
        float currentAccumulatedY = 0;

        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            const TransformNode* node = *it;

            // Локальные координаты этого узла относительно его непосредственного родителя
            // должны быть повернуты на глобальный угол родителя (который уже накоплен в currentAccumulatedAngle)

            float cosA = std::cos(currentAccumulatedAngle);
            float sinA = std::sin(currentAccumulatedAngle);

            float rotatedLocalX = node->position.x * cosA - node->position.y * sinA;
            float rotatedLocalY = node->position.x * sinA + node->position.y * cosA;

            currentAccumulatedX += rotatedLocalX;
            currentAccumulatedY += rotatedLocalY;
            currentAccumulatedAngle += node->angle;
        }

        return {currentAccumulatedX, currentAccumulatedY, currentAccumulatedAngle};
    }

    // --- Управление Спрайтами ---

    size_t addSprite(SpriteData data, size_t transformIdx = ~size_t{0})
    {
        // 1. Добавляем данные в плотный массив
        const size_t new_data_idx = sprite_data_.size();
        sprite_data_.push_back(data);

        // 2. Получаем слот в векторе sprites_
        size_t sprite_idx;
        if (!free_sprite_indices_.empty()) {
            sprite_idx = free_sprite_indices_.front();
            free_sprite_indices_.pop();
            // Сброс старого состояния
            sprites_[sprite_idx] = Sprite{};
        } else {
            sprite_idx = sprites_.size();
            sprites_.emplace_back();
        }

        Sprite& s = sprites_[sprite_idx];
        s.dataIdx     = new_data_idx;
        s.transformIdx = transformIdx;
        // s.active больше нет. Валидность определяется наличием dataIdx != ~0

        return sprite_idx;
    }

    void removeSprite(size_t sprite_idx)
    {
        if (sprite_idx >= sprites_.size()) return;

        Sprite& spr = sprites_[sprite_idx];
        if (!spr.isValid()) return; // Уже удален

        const size_t data_idx_to_remove = spr.dataIdx;
        const size_t last_data_idx      = sprite_data_.size() - 1;

        // Swap-and-pop для sprite_data_
        if (data_idx_to_remove != last_data_idx) {
            sprite_data_[data_idx_to_remove] = sprite_data_[last_data_idx];

            // Обновляем ссылку у спрайта, который ссылался на последний элемент
            // Это O(N) по количеству спрайтов.
            // Если это узкое место, нужно хранить обратный индекс в SpriteData или использовать map.
            for (Sprite& s : sprites_) {
                if (s.isValid() && s.dataIdx == last_data_idx) {
                    s.dataIdx = data_idx_to_remove;
                    // break;
                }
            }
        }
        sprite_data_.pop_back();

        // Освобождаем слот спрайта
        spr.dataIdx = ~size_t{0};
        spr.transformIdx = ~size_t{0};
        free_sprite_indices_.push(sprite_idx);
    }

    // --- Доступ к данным для GPU ---

    const SpriteData* getDataPointer() const
    {
        return sprite_data_.empty() ? nullptr : sprite_data_.data();
    }

    SpriteData* getDataPointer(size_t sprite_idx)
    {
        return &sprite_data_[sprites_[sprite_idx].dataIdx];
    }

    size_t getDataSize() const { return sprite_data_.size(); }

    // Для отправки трансформаций в GPU (SSBO)
    const TransformNode* getTransformsPointer() const {
        return transforms_.empty() ? nullptr : transforms_.data();
    }

    size_t getTransformsCount() const {
        return transforms_.size();
    }

    TransformNode& getTransform(size_t local_transform_)
    {
        return transforms_[local_transform_];
    }

    // Прямой доступ
    SpriteData& dataFromSpriteIdx(size_t index)
    {
        return sprite_data_[sprites_[index].dataIdx];
    }

    float getGlobalRotation(size_t transform)
    {
        float rotation = 0;

        while (transform != ~size_t{0}) {
            auto& transform_ref = transforms_[transform];
            rotation += transform_ref.angle;
            transform = transform_ref.parentIndex;
        }
        return rotation;
    }

    glm::vec2 getGlobalPosition(size_t transform)
    {
        glm::vec2 position{};
        glm::vec2 rotated_position{};

        while (transform != ~size_t{0}) {
            auto& transform_ref = transforms_[transform];
            float cosA = std::cosf(transform_ref.angle);
            float sinA = std::sinf(transform_ref.angle);
            rotated_position.x = position.x * cosA - position.y * sinA;
            rotated_position.y = position.x * sinA + position.y * cosA;
            rotated_position += transform_ref.position;
            position = rotated_position;
            transform = transform_ref.parentIndex;
        }
        return position;
    }

    Sprite& operator[](size_t index) { return sprites_[index]; }

    const Sprite& operator[](size_t index) const { return sprites_[index]; }

    // --- static part

    inline static SpritesRepository* active_repo;

    static SpritesRepository& get_active()
    {
        return *active_repo;
    }
    static void set_active(SpritesRepository* repo)
    {
        active_repo = repo;
    }
};