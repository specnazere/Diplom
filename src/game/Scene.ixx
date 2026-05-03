module;

#include "glm/glm.hpp"

#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <utility>

export module scene;

import destructible_object;
import sprite_repository;
import texture;
import atlas;

// ---------------------------------------------------------------------------
//  ObjectHandle — идентификатор объекта в сцене.
//  Поколение (generation) защищает от обращения к удалённому слоту.
// ---------------------------------------------------------------------------
export struct ObjectHandle
{
    uint32_t index      = ~uint32_t{0};
    uint32_t generation = 0;

    bool is_valid() const { return index != ~uint32_t{0}; }
    bool operator==(const ObjectHandle&) const = default;
};

// ---------------------------------------------------------------------------
//  CollisionEvent — информация о столкновении двух объектов.
//  Передаётся в пользовательский коллбэк.
// ---------------------------------------------------------------------------
export struct CollisionEvent
{
    ObjectHandle          a;
    ObjectHandle          b;
    std::vector<ContactPoint> contacts; // точки контакта в мировых координатах
};

// ---------------------------------------------------------------------------
//  CollisionResponse — что сцена должна сделать после столкновения.
//  Возвращается из пользовательского коллбэка.
// ---------------------------------------------------------------------------
export struct CollisionResponse
{
    float damage_to_a = 0.f;  // урон объекту A
    float damage_to_b = 0.f;  // урон объекту B
    float damage_radius = 0.f; // радиус применения урона
};

// ---------------------------------------------------------------------------
//  Scene
//
//  Управляет набором DestructibleObject-ов.
//  Каждый кадр:
//    1. Собирает сигнатуры (позиция + радиус описывающей окружности).
//    2. Broad-phase: проверяет все пары сигнатур — O(N²) с ранним выходом.
//    3. Narrow-phase: вызывает find_contacts() только для пересекающихся пар.
//    4. Для каждого столкновения вызывает пользовательский коллбэк,
//       применяет возвращённый урон и собирает фрагменты.
//    5. Добавляет фрагменты в сцену, удаляет полностью уничтоженные объекты.
// ---------------------------------------------------------------------------
export class Scene
{
public:
    // Пользовательский коллбэк на столкновение.
    // Получает событие, возвращает желаемый отклик (урон).
    // Если коллбэк не задан — столкновения только обнаруживаются, урон не наносится.
    using CollisionCallback = std::function<CollisionResponse(const CollisionEvent&)>;

    explicit Scene(CollisionCallback on_collision = nullptr)
        : on_collision_(std::move(on_collision))
    {}

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&)                 = default;
    Scene& operator=(Scene&&)      = default;

    // -----------------------------------------------------------------------
    //  Добавить объект в сцену. Возвращает хэндл для последующего доступа.
    // -----------------------------------------------------------------------
    ObjectHandle add(std::unique_ptr<DestructibleObject> obj)
    {
        assert(obj != nullptr);

        // Ищем свободный слот среди удалённых
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied)
            {
                slots_[i].obj      = std::move(obj);
                slots_[i].occupied = true;
                // generation уже инкрементирован при удалении
                return { i, slots_[i].generation };
            }
        }

        // Нет свободных слотов — расширяем
        const uint32_t idx = static_cast<uint32_t>(slots_.size());
        slots_.push_back({ std::move(obj), 0, true });
        return { idx, 0 };
    }

    // -----------------------------------------------------------------------
    //  Удалить объект по хэндлу. Возвращает false если хэндл устарел.
    // -----------------------------------------------------------------------
    bool remove(ObjectHandle handle)
    {
        if (!is_alive(handle)) return false;
        free_slot(handle.index);
        return true;
    }

    // -----------------------------------------------------------------------
    //  Получить указатель на объект (nullptr если хэндл устарел).
    // -----------------------------------------------------------------------
    DestructibleObject* get(ObjectHandle handle)
    {
        if (!is_alive(handle)) return nullptr;
        return slots_[handle.index].obj.get();
    }
    const DestructibleObject* get(ObjectHandle handle) const
    {
        if (!is_alive(handle)) return nullptr;
        return slots_[handle.index].obj.get();
    }

    // -----------------------------------------------------------------------
    //  Проверить, жив ли хэндл
    // -----------------------------------------------------------------------
    bool is_alive(ObjectHandle handle) const
    {
        return handle.index < slots_.size()
            && slots_[handle.index].occupied
            && slots_[handle.index].generation == handle.generation;
    }

    // -----------------------------------------------------------------------
    //  Установить/убрать коллбэк столкновения
    // -----------------------------------------------------------------------
    void set_collision_callback(CollisionCallback cb)
    {
        on_collision_ = std::move(cb);
    }

    // -----------------------------------------------------------------------
    //  Обновить сцену за один кадр.
    //
    //  Алгоритм:
    //    1. Собрать сигнатуры всех живых объектов.
    //    2. Broad-phase по сигнатурам (описывающие окружности).
    //    3. Narrow-phase через find_contacts() для прошедших broad-phase.
    //    4. Применить коллбэк и урон.
    //    5. Вызвать split_if_disconnected() на повреждённых объектах,
    //       добавить фрагменты в сцену.
    //    6. Удалить объекты, у которых не осталось живых вокселей.
    // -----------------------------------------------------------------------
    void update(float dt)
    {
        rebuild_signatures();

        // Пары, прошедшие broad-phase
        std::vector<std::pair<uint32_t, uint32_t>> candidates;
        broad_phase(candidates);

        // Множество слотов, которые получили урон — им нужен split
        std::vector<uint32_t> damaged_slots;

        // Narrow-phase
        for (auto [ia, ib] : candidates)
        {
            DestructibleObject& a = *slots_[ia].obj;
            DestructibleObject& b = *slots_[ib].obj;

            ContactPoint contact = a.find_contacts(b);

            if (contact.normal == glm::vec2(0, 0)) continue;

            // 1. Подготовка данных
            const float restitution = 0.8f; // Попробуй разные значения: 0.0, 0.5, 0.9
            const float percent = 1.f;     // Процент коррекции позиции (0.8 = 80% проникновения убираем за кадр)
            const float slop = 0.01f;       // Допуск проникновения (меньше этого значения не корректируем, чтобы избежать дрожания)

            const glm::vec2 posA = a.get_position();
            const glm::vec2 posB = b.get_position();

            const float mA = a.get_mass();
            const float mB = b.get_mass();
            const float IA = a.get_inertia_moment();
            const float IB = b.get_inertia_moment();

            // Векторы от центров масс до точки контакта
            const glm::vec2 rA = contact.world_pos - posA;
            const glm::vec2 rB = contact.world_pos - posB;

            // 2. Расчет скоростей в точке контакта
            const glm::vec2 vA = a.velocity + glm::vec2(-a.angular_velocity * rA.y, a.angular_velocity * rA.x);
            const glm::vec2 vB = b.velocity + glm::vec2(-b.angular_velocity * rB.y, b.angular_velocity * rB.x);

            const glm::vec2 vRel = vA - vB;

            glm::vec2 n = contact.normal;
            float lenN = glm::length(n);
            if (lenN < 1e-6f) continue;
            n /= lenN;

            // Проекция относительной скорости на нормаль
            float velAlongNormal = glm::dot(vRel, n);

            // Если тела разлетаются, не применяем импульс (но коррекция позиции может быть нужна!)
            // Для чистоты эксперимента пока оставим выход, если разлетаются
            // if (velAlongNormal > 0)
            // {
            //     // Опционально: можно сделать коррекцию позиции даже при разлете, если есть проникновение
            //     // Но для начала оставим так
            //     continue;
            // }

            // 3. Расчет импульса (J)
            const float raCrossN = rA.x * n.y - rA.y * n.x;
            const float rbCrossN = rB.x * n.y - rB.y * n.x;

            const float invMassSum = (1.0f / mA) + (1.0f / mB);
            const float invInertiaSum = (raCrossN * raCrossN) / IA + (rbCrossN * rbCrossN) / IB;

            if (invMassSum + invInertiaSum < 1e-9f) continue;

            float j = -(1.0f + restitution) * velAlongNormal;
            j /= (invMassSum + invInertiaSum);

            const glm::vec2 impulse = n * j;

            // 4. Применение импульса к скоростям
            a.velocity += impulse * (1.0f / mA);
            b.velocity -= impulse * (1.0f / mB);

            a.angular_velocity += (rA.x * impulse.y - rA.y * impulse.x) / IA;
            b.angular_velocity -= (rB.x * impulse.y - rB.y * impulse.x) / IB;

            // 5. Позиционная коррекция (ВАЖНО!)
            // Рассчитываем глубину проникновения.
            // В твоем случае, так как это воксели, точную глубину сложно взять из одной точки.
            // Но мы можем оценить её через относительную скорость и dt, или просто сдвинуть на фиксированную величину,
            // если известно, что они пересекаются.

            // Упрощенный вариант: сдвигаем тела вдоль нормали, чтобы уменьшить перекрытие.
            // Так как у нас нет явной глубины проникновения из find_contacts,
            // используем эвристику: если был удар, значит было перекрытие.
            // Стандартный подход в импульсной физике:
            // correction = max(penetration - slop, 0) / (invMassA + invMassB) * percent

            // Поскольку ты не передаешь глубину проникновения, давай оценим её грубо:
            // Если мы здесь, значит воксели пересеклись.
            // Для стабильности можно просто слегка раздвинуть объекты вдоль нормали.

            // Вариант А: Если бы у тебя была глубина (penetration):
            // float penetration = ...;
            // float correction_mag = std::max(penetration - slop, 0.0f) / (invMassSum) * percent;

            // Вариант Б (для твоего кода):
            // Так как точная глубина неизвестна, но мы знаем, что контакт есть,
            // можно использовать "баг-фикс" от дрожания:
            // Если скорость после удара очень мала, а объекты все еще близко, их может "зажевать".

            // Давай добавим простую коррекцию, основанную на том, что мы хотим убрать относительную скорость проникновения.
            // Но лучше всего — передать глубину из find_contacts.

            // Пока что, чтобы увидеть эффект, попробуй просто жестко раздвинуть на малую величину,
            // если коэффициент упругости высокий, чтобы они не "залипали".

            if (restitution > 0.1f) {
                 const float correctionMag = 0.05f; // Подбери экспериментально (в единицах твоего мира)
                 const glm::vec2 correction = n * correctionMag;
                 a.add_position(correction * (1.0f / mA) / invMassSum * percent); // Распределение по массам
                 b.add_position(-correction * (1.0f / mB) / invMassSum * percent);
            }
        }

        // Убираем дубликаты в damaged_slots
        std::sort(damaged_slots.begin(), damaged_slots.end());
        damaged_slots.erase(std::unique(damaged_slots.begin(), damaged_slots.end()),
                            damaged_slots.end());

        // Split + добавление фрагментов
        std::vector<std::unique_ptr<DestructibleObject>> new_fragments;

        for (uint32_t idx : damaged_slots)
        {
            if (!slots_[idx].occupied) continue;

            auto frags = slots_[idx].obj->split_if_disconnected();
            for (auto& frag : frags)
                new_fragments.push_back(
                    std::make_unique<DestructibleObject>(std::move(frag)));
        }

        // Удалить полностью уничтоженные объекты
        remove_dead_objects();

        // Добавить фрагменты (после remove, чтобы не трогать их сразу же)
        for (auto& frag : new_fragments)
            add(std::move(frag));

        for (auto& slot : slots_) {
            slot.obj->update_position(dt);
        }
    }

    // -----------------------------------------------------------------------
    //  Итерация по всем живым объектам
    // -----------------------------------------------------------------------
    template<typename Fn>
    void for_each(Fn&& fn)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied) continue;
            fn(ObjectHandle{ i, slots_[i].generation }, *slots_[i].obj);
        }
    }

    template<typename Fn>
    void for_each(Fn&& fn) const
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied) continue;
            fn(ObjectHandle{ i, slots_[i].generation },
               std::as_const(*slots_[i].obj));
        }
    }

    size_t object_count() const
    {
        size_t n = 0;
        for (auto& s : slots_) if (s.occupied) ++n;
        return n;
    }

private:
    // -----------------------------------------------------------------------
    //  Слот хранения объекта
    // -----------------------------------------------------------------------
    struct Slot
    {
        std::unique_ptr<DestructibleObject> obj;
        uint32_t generation = 0;
        bool     occupied   = false;
    };

    // -----------------------------------------------------------------------
    //  Сигнатура объекта — позиция и радиус описывающей окружности
    //  в мировом пространстве. Не зависит от угла поворота.
    //
    //  Пересчитывается каждый кадр, потому что объект может двигаться.
    //  Радиус пересчитывается только если объект получил урон (дорогая
    //  операция не нужна каждый кадр — он зависит только от размера сетки).
    // -----------------------------------------------------------------------
    struct Signature
    {
        uint32_t  slot_idx;
        glm::vec2 position;
        float     radius;
    };

    std::vector<Slot>      slots_;
    std::vector<Signature> signatures_; // живёт один кадр, пересобирается в update()
    CollisionCallback      on_collision_;

    // -----------------------------------------------------------------------
    //  Пересобрать сигнатуры для всех живых объектов
    // -----------------------------------------------------------------------
    void rebuild_signatures()
    {
        signatures_.clear();
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied) continue;
            const DestructibleObject& obj = *slots_[i].obj;
            signatures_.push_back({
                i,
                obj.get_position(),
                obj.get_bounding_radius()
            });
        }
    }

    // -----------------------------------------------------------------------
    //  Broad-phase: проверяем все пары сигнатур.
    //
    //  Два объекта — кандидаты на коллизию, если суммарный радиус их
    //  описывающих окружностей больше расстояния между центрами:
    //
    //      dist(A.position, B.position) < A.radius + B.radius
    //
    //  Проверка по квадрату расстояния — без sqrt.
    // -----------------------------------------------------------------------
    void broad_phase(std::vector<std::pair<uint32_t, uint32_t>>& out) const
    {
        const size_t n = signatures_.size();
        for (size_t i = 0; i < n; ++i)
        {
            const Signature& sa = signatures_[i];

            for (size_t j = i + 1; j < n; ++j)
            {
                const Signature& sb = signatures_[j];

                const glm::vec2 d = sa.position - sb.position;
                const float dist2 = d.x * d.x + d.y * d.y;
                const float rsum  = sa.radius + sb.radius;

                if (dist2 < rsum * rsum)
                    out.push_back({ sa.slot_idx, sb.slot_idx });
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Удалить объекты без живых вокселей
    // -----------------------------------------------------------------------
    void remove_dead_objects()
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied) continue;

            const auto& voxels = slots_[i].obj->voxels();
            const bool has_alive = std::any_of(
                voxels.begin(), voxels.end(),
                [](const Voxel& vx) { return vx.is_alive(); });

            if (!has_alive)
                free_slot(i);
        }
    }

    // -----------------------------------------------------------------------
    //  Освободить слот, инкрементировав поколение
    // -----------------------------------------------------------------------
    void free_slot(uint32_t idx)
    {
        slots_[idx].obj.reset();
        slots_[idx].occupied = false;
        ++slots_[idx].generation; // инвалидирует все старые хэндлы на этот слот
    }
};
