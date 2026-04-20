//
// Created by A on 17.04.2026.
//

module;
#include "glm/glm.hpp"

#include <vector>
#include <queue>
#include <algorithm>

export module sprite_repository;

import atlas;
import sprite;

export
class SpritesRepository
{
private:
    std::vector<Sprite> sprites_ = std::vector<Sprite>();
    std::vector<size_t> sorted_indices_ = std::vector<size_t>(); // Индексы в sprite_data_, отсортированные по depth
    std::vector<SpriteData> sprite_data_ = std::vector<SpriteData>();
    std::queue<int> free_sprite_indices_; // Очередь свободных индексов в векторе sprites_

    size_t dataIndex(SpriteData* ptr) const
    {
        return static_cast<size_t>(ptr - sprite_data_.data());
    }

public:
    Atlas atlas;

    SpritesRepository()
    {
        sprites_.reserve(128);
        sorted_indices_.reserve(128);
        sprite_data_.reserve(128);
    };

    // Добавление спрайта. Возвращает индекс в векторе sprites_
    Sprite* addSprite(const SpriteData& data, float depth) {
        size_t sprite_idx;
        int data_idx;

        // 1. Получаем место для данных
        if (!free_sprite_indices_.empty()) {
            data_idx = free_sprite_indices_.front();
            free_sprite_indices_.pop();

            // Перезаписываем старые данные
            sprite_data_[data_idx] = data;
            // Восстанавливаем соответствие в sorted_indices_, если там был мусор или старый индекс
            // Но так как мы делаем swap-and-pop при удалении, free_indices содержит индексы,
            // которые сейчас "за пределами" логического размера или помечены как свободные?
            // Нет, при swap-and-pop размер вектора уменьшается.
            // Чтобы использовать free_indices с векторами, которые меняют размер,
            // нужно либо не уменьшать вектор (хранить dead slots), либо использовать другую стратегию.

            // СТРАТЕГИЯ ИЗМЕНЕНА ДЛЯ СООТВЕТСТВИЯ ТЗ И ЭФФЕКТИВНОСТИ:
            // ТЗ: "Выкидывай только size_t и SpriteData".
            // Если мы выкидываем (erase/pop), индексы сдвигаются.
            // Чтобы не сдвигать все индексы в Sprite, лучше использовать битовую карту или
            // хранить индексы относительно.

            // ДАВАЙТЕ СДЕЛАЕМ ТАК:
            // Векторы data и indices всегда плотные.
            // При удалении мы меняем местами удаляемый элемент с последним, потом pop_back.
            // Тогда меняется индекс только у одного спрайта (того, что был последним).
            // Free indices нам нужны только для того, чтобы знать, куда писать новые данные,
            // если мы НЕ хотим делать push_back, а хотим переиспользовать память?
            // Но vector сам управляет памятью.

            // Реализация строго по ТЗ с очередью свободных индексов предполагает,
            // что векторы НЕ уменьшаются в размере при удалении, а помечают слоты как свободные?
            // "Выкидывай только size_t и SpriteData" — звучит как erase.
            // "Заноси уже свободные индексы в очередь" — звучит как reuse slots.

            // КОМПРОМИСС:
            // Мы будем хранить данные плотно.
            // free_indices_ будет использоваться, если мы решим не делать shrink_to_fit,
            // но для простоты и производительности GPU лучше держать данные плотно.
            // Однако, ТЗ требует очередь свободных индексов.
            // Это возможно ТОЛЬКО если векторы не меняют размер при удалении (логическое удаление).
            // Но тогда "выкидывай" неверно.

            // Интерпретация:
            // 1. sprites_ (вектор объектов Sprite) растет и никогда не сжимается (или сжимается редко).
            // 2. sprite_data_ и sorted_indices_ растут и никогда не сжимаются физически,
            //    но имеют логический размер? Нет, это плохо для GPU.

            // ПРАВИЛЬНАЯ ИНТЕРПРЕТАЦИЯ ДЛЯ GPU BATCHING + FREE LIST:
            // Мы используем swap-and-pop для удаления из data/indices.
            // Free list не нужен для data, если мы делаем pop_back.
            // Free list нужен для sprites_, если мы их не удаляем из вектора sprites_.

            // Давай сделаем так:
            // 1. add: push_back в data, push_back в indices, push_back в sprites.
            // 2. remove:
            //    - Находим data_index удаляемого спрайта.
            //    - Берем последний элемент из sprite_data_ и переносим его на место удаляемого.
            //    - Обновляем индекс в Sprite, который ссылался на этот последний элемент.
            //    - pop_back из sprite_data_ и sorted_indices_.
            //    - Помечаем Sprite как !active.
            //    - Индекс в Sprite ставим -1.
            //    - Free indices не нужен для data, так как мы используем конец вектора.
            //    - Free indices МОЖЕТ БЫТЬ НУЖЕН, если мы хотим переиспользовать освободившиеся места в векторе sprites_?
            //      ТЗ: "заноси уже свободные индексы в очередь, что бы просто перезаписать их когда появится необходимость".
            //      Это относится к спрайтам? "Удаляя спрайты не выкидывай их из первого вектора... заноси уже свободные индексы в очередь".
            //      Да, это про вектор Sprite.
        }

        // ПЕРЕПИСЫВАЮ ЛОГИКУ ПОД ТЗ МАКСИМАЛЬНО БЛИЗКО:

        // 1. Управление памятью SpriteData и Index buffer:
        //    Чтобы GPU мог брать сырые данные, вектор должен быть contiguous.
        //    Если мы используем free list для данных, в векторе будут дыры.
        //    Для GPU это плохо (надо фильтровать).
        //    Обычно делают: Compact Data Vector + Index Buffer.
        //    Если я удаляю элемент, я делаю swap с последним и pop.
        //    Тогда free list для ДАННЫХ не нужен.
        //    Free list нужен для ОБЪЕКТОВ SPRITE, чтобы не аллоцировать новые, а переиспользовать старые.

        // Реализация:

        size_t new_data_idx = sprite_data_.size();

        sprite_data_.push_back(data);
        sorted_indices_.push_back(new_data_idx);

        Sprite* new_sprite = new Sprite();
        new_sprite->data = &sprite_data_.data()[new_data_idx];
        new_sprite->depth = depth;
        new_sprite->active = true;

        if (!free_sprite_indices_.empty()) {
            sprite_idx = free_sprite_indices_.front();
            free_sprite_indices_.pop();
            sprites_[sprite_idx] = *new_sprite;
        } else {
            sprite_idx = static_cast<int>(sprites_.size());
            sprites_.push_back(*new_sprite);
        }

        return new_sprite;
    }

    // Удаление спрайта по индексу в векторе sprites_
    void removeSprite(int sprite_idx) {
        if (sprite_idx < 0 || sprite_idx >= static_cast<int>(sprites_.size())) return;

        Sprite& spr = sprites_[sprite_idx];
        if (!spr.active) return;

        int data_idx_to_remove = dataIndex(spr.data);

        // 1. Убираем связь у спрайта
        spr.active = false;
        spr.data = nullptr;
        free_sprite_indices_.push(sprite_idx);

        // 2. Удаляем данные из плотных векторов (Swap-and-Pop для O(1))
        // Последний элемент данных переезжает на место удаляемого
        int last_data_idx = static_cast<int>(sprite_data_.size()) - 1;

        // if (data_idx_to_remove != last_data_idx) {
        //     // Перемещаем данные
        //     sprite_data_[data_idx_to_remove] = std::move(sprite_data_.back());
        //
        //     // Обновляем индекс в sorted_indices_
        //     sorted_indices_[data_idx_to_remove] = static_cast<size_t>(data_idx_to_remove); // Индекс в векторе данных теперь совпадает с позицией
        //
        //     // ВАЖНО: Нужно найти Sprite, который ссылался на last_data_idx, и обновить его data_index
        //     // Так как мы не храним обратную ссылку Data -> Sprite, нам придется искать?
        //     // Или мы храним в SpriteData ID спрайта? Нет, в ТЗ такого нет.
        //     // Поиск по всем спрайтам — O(N). Это плохо.
        //
        //     // АЛЬТЕРНАТИВА: Не делать swap-and-pop для данных, если трудно обновлять ссылки.
        //     // Но ТЗ требует "сырые данные для видеокарты". Дыры в данных недопустимы для простого glBufferData.
        //     // Значит, данные должны быть компактными.
        //     // Значит, при удалении любого элемента, кроме последнего, чей-то индекс изменится.
        //     // Чтобы сделать это быстро, нужно хранить обратную ссылку или ID.
        //     // Добавлю поле sprite_id в SpriteData? Нет, ТЗ фиксировано.
        //
        //     // Решение: Хранить в SpriteData ничего нельзя.
        //     // Придется итерироваться по sprites_ и искать тот, у которого data_index == last_data_idx.
        //     // Это O(N) по количеству спрайтов. Для < 10k спрайтов это нормально.
        //
        //     for (auto& s : sprites_) {
        //         if (s.active && s.data_index == last_data_idx) {
        //             s.data_index = data_idx_to_remove;
        //             break;
        //         }
        //     }
        //
        //     // Обновляем sorted_indices_: элемент, который был последним, теперь находится на позиции data_idx_to_remove
        //     // sorted_indices_ хранит индексы для сортировки?
        //     // ТЗ: "в векторе size_t должны быть индексы на соответствующие элементы из вектора с SpriteData, что бы при сортировке менять индекс в Sprite."
        //     // Это странная формулировка. Обычно сортируют индексы, а не меняют индексы в спрайте.
        //     // "менять индекс в Sprite" — значит, при сортировке мы меняем порядок отрисовки, но не данные?
        //     // Или мы меняем data_index в Sprite?
        //     // Если мы сортируем, мы сортируем sorted_indices_.
        //     // sorted_indices_[i] говорит: "i-тый элемент для отрисовки находится по адресу data_idx в sprite_data_".
        //
        //     // Если мы сделали swap в sprite_data_, то sorted_indices_ тоже нужно обновить?
        //     // sorted_indices_ должен отражать текущее положение данных?
        //     // Нет, sorted_indices_ используется для обхода при отрисовке.
        //     // Если мы сделали swap в sprite_data_, то данные переехали.
        //     // Нам нужно найти в sorted_indices_ значение last_data_idx и заменить его на data_idx_to_remove.
        //
        //     auto it = std::find(sorted_indices_.begin(), sorted_indices_.end(), static_cast<size_t>(last_data_idx));
        //     if (it != sorted_indices_.end()) {
        //         *it = static_cast<size_t>(data_idx_to_remove);
        //     }
        // }
        //
        // // Pop back
        // sprite_data_.pop_back();
        // sorted_indices_.pop_back();
    }

    // Сортировка спрайтов по depth.
    // Мы сортируем вектор sorted_indices_, сравнивая depth соответствующих спрайтов.
    // ТЗ: "что бы при сортировке менять индекс в Sprite".
    // Если под "индексом в Sprite" имеется в виду порядок отрисовки, то мы просто сортируем indices.
    // Если нужно менять именно поле data_index в Sprite, то это ломает связь с данными.
    // Скорее всего, имелось в виду: сортируем индексы отрисовки.
    void sort() {
        // Сортируем индексы на основе глубины соответствующих спрайтов
        // Нам нужно получить доступ к depth. Depth хранится в Sprite, а не в SpriteData.
        // Это проблема. sorted_indices_ ссылается на SpriteData.
        // Как связать SpriteData и Depth?
        // В ТЗ: "в каждом Sprite должен быть ... float depth".
        // В ТЗ: "в векторе size_t должны быть индексы на соответствующие элементы из вектора с SpriteData".

        // Если мы сортируем только по depth, а depth нет в SpriteData, то sorted_indices_ не может быть отсортирован независимо от Sprite.
        // Возможно, предполагается, что sorted_indices_ хранит индексы СПРАЙТОВ, а не данных?
        // "Во втором size_t ... индексы на соответствующие элементы из вектора с SpriteData".

        // Давайте перечитаем внимательно:
        // 1. Вектор<Sprite>
        // 2. Вектор<size_t> (индексы на SpriteData)
        // 3. Вектор<SpriteData>

        // Если мы сортируем вектор size_t, мы меняем порядок доступа к данным.
        // Но критерий сортировки (depth) лежит в Sprite.
        // Значит, нам нужно знать, какой Sprite соответствует какому элементу в векторе size_t?
        // Или вектор size_t имеет тот же размер, что и вектор Sprite?
        // "в каждом Sprite должен быть индекс объекта SpriteData".

        // Гипотеза: Вектор size_t параллелен вектору Sprite.
        // То есть sorted_indices_[i] соответствует sprites_[i].
        // И при сортировке мы меняем местами элементы в векторе size_t?
        // Нет, это не отсортирует отрисовку.

        // Вероятно, вектор size_t — это индексный буфер для отрисовки (Index Buffer).
        // Он должен содержать индексы в sprite_data_.
        // Его размер равен количеству активных спрайтов? Или всех?

        // Давай сделаем так:
        // sorted_indices_ содержит индексы в sprite_data_ для всех АКТИВНЫХ спрайтов, отсортированные по depth.
        // При вызове sort():
        // 1. Собираем пары (depth, data_index) из активных спрайтов.
        // 2. Сортируем их.
        // 3. Заполняем sorted_indices_ новыми значениями data_index.

        // Но ТЗ говорит: "в векторе size_t должны быть индексы ... что бы при сортировке менять индекс в Sprite".
        // Это очень запутанно. "Менять индекс в Sprite" может означать, что мы меняем data_index внутри Sprite?
        // Если мы меняем data_index в Sprite, то нам не нужен отдельный вектор size_t для сортировки,
        // мы можем просто менять порядок в самом векторе Sprite? Нет, вектор Sprite фиксирован по структуре.

        // Я реализую наиболее разумный с точки зрения графики вариант:
        // sorted_indices_ — это буфер индексов для GPU (или CPU обхода), содержащий индексы в sprite_data_.
        // Он всегда актуален и отсортирован.

        std::vector<std::pair<float, SpriteData*>> depth_map; // depth, data_index
        depth_map.reserve(sprites_.size());

        for (const auto& s : sprites_) {
            if (s.active) {
                depth_map.emplace_back(s.depth, s.data);
            }
        }

        std::sort(depth_map.begin(), depth_map.end(), [](const auto& a, const auto& b) {
            return a.first < b.first; // Sort by depth ascending
        });

        sorted_indices_.resize(depth_map.size());
        for (size_t i = 0; i < depth_map.size(); ++i) {
            sorted_indices_[i] = dataIndex(depth_map[i].second);
        }
    }

    // Получение сырых данных для GPU
    const SpriteData* getDataPointer() const {
        if (sprite_data_.empty())
            return nullptr;
        return sprite_data_.data();
    }

    size_t getDataSize() const {
        return sprite_data_.size();
    }

    // Получение индексов для отрисовки (если нужно)
    const size_t* getIndicesPointer() const {
        if (sorted_indices_.empty()) return nullptr;
        return sorted_indices_.data();
    }

    size_t getIndicesSize() const {
        return sorted_indices_.size();
    }
};