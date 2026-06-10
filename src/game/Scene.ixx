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
#include <mutex> // Added

export module scene;

import destructible_object;
import sprite_repository;
import texture;
import atlas;
import projectile;

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
    std::vector<glm::vec2> contacts; // точки контакта в мировых координатах
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
    using CollisionCallback = std::function<CollisionResponse(const CollisionEvent&)>;

private:
    struct Slot
    {
        std::unique_ptr<DestructibleObject> obj;
        uint32_t generation = 0;
        bool occupied   = false;
        bool has_physics = true;
    };

    struct ProjectileSlot
    {
        std::unique_ptr<projectile> proj;

    };

    struct Signature
    {
        uint32_t  slot_idx;
        glm::vec2 position;
        float     radius;
    };

    std::vector<Slot>      slots_;
    std::vector<ProjectileSlot> projectiles_;
    std::vector<Signature> signatures_; // живёт один кадр, пересобирается в update()

    CollisionCallback      on_collision_;
    mutable std::mutex     mutex_;       // Added

public:

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
    ObjectHandle add(std::unique_ptr<DestructibleObject> obj, bool has_physic = true)
    {
        assert(obj != nullptr);

        std::lock_guard<std::mutex> lock(mutex_);
        // Ищем свободный слот среди удалённых
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
        {
            if (!slots_[i].occupied)
            {
                slots_[i].obj = std::move(obj);
                slots_[i].occupied = true;
                slots_[i].has_physics = has_physic;
                // generation уже инкрементирован при удалении
                return { i, slots_[i].generation };
            }
        }

        // Нет свободных слотов — расширяем
        const uint32_t idx = static_cast<uint32_t>(slots_.size());
        slots_.push_back({ std::move(obj), 0, true, has_physic});
        return { idx, 0 };
    }

    // -----------------------------------------------------------------------
    //  Удалить объект по хэндлу. Возвращает false если хэндл устарел.
    // -----------------------------------------------------------------------
    bool remove(ObjectHandle handle)
    {
        if (!is_alive(handle)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        free_slot(handle.index);
        return true;
    }

    void spawnProjectile(const projectile& prototype)
    {
        projectiles_.push_back({std::make_unique<projectile>(prototype)});
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
    //  Manual lock/unlock for external synchronization
    // -----------------------------------------------------------------------
    void lock()
    {
        mutex_.lock();
    }

    void unlock()
    {
        mutex_.unlock();
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
        std::lock_guard<std::mutex> lock(mutex_);

        rebuild_signatures();

        // Пары, прошедшие broad-phase
        std::vector<std::pair<uint32_t, uint32_t>> candidates;
        broad_phase(candidates);

        // Множество слотов, которые получили урон — им нужен split
        std::vector<uint32_t> damaged_slots;

        // Narrow-phase
        for (auto [ia, ib] : candidates) {
            DestructibleObject& a = *slots_[ia].obj;
            DestructibleObject& b = *slots_[ib].obj;

            if (!(slots_[ib].has_physics && slots_[ia].has_physics))
                continue;

            std::optional<glm::vec2> contact = a.find_contacts(b);
            if (!contact.has_value()) continue;

            glm::vec2& contact_point = contact.value();

            // 1. Получаем реакцию от пользователя (урон, тип взаимодействия)
            CollisionEvent event{ {ia, slots_[ia].generation}, {ib, slots_[ib].generation}, {contact_point} };
            CollisionResponse response = on_collision_ ? on_collision_(event) : CollisionResponse{};

            // 2. Наносим урон
            if (response.damage_to_a > 0) a.apply_damage(contact_point, response.damage_to_a, response.damage_radius);
            if (response.damage_to_b > 0) b.apply_damage(contact_point, response.damage_to_b, response.damage_radius);

            // 3. Физика
            // Если один из объектов "нефизический" (например, пуля, которая должна исчезнуть),
            // мы можем обработать это особо.

            // bool a_is_bullet = (a.type == ObjectType::Bullet); // Предположим, ты добавил enum
            // bool b_is_bullet = (b.type == ObjectType::Bullet);

            // if (a_is_bullet || b_is_bullet) {
            //     // Специфичная логика для пуль
            //     if (a_is_bullet) {
            //         // Пуля А бьет Корабль Б
            //         // Корабль получает импульс
            //         glm::vec2 impulse = contact.normal * some_bullet_impulse_strength;
            //         b.apply_impulse_at(impulse, contact.world_pos);
            //         // Пуля помечается на удаление (или удаляется сразу, если allow_remove_in_update)
            //         mark_for_removal(ia);
            //     } else {
            //         // Пуля Б бьет Корабль А
            //         glm::vec2 impulse = -contact.normal * some_bullet_impulse_strength;
            //         a.apply_impulse_at(impulse, contact.world_pos);
            //         mark_for_removal(ib);
            //     }
            // } else {

            const float restitution = 0.6f; // Попробуй разные значения: 0.0, 0.5, 0.9
            const float percent = -0.1f;     // Процент коррекции позиции (0.8 = 80% проникновения убираем за кадр)
            const float slop = 0.01f;       // Допуск проникновения (меньше этого значения не корректируем, чтобы избежать дрожания)

            const glm::vec2 posA = a.get_position();
            const glm::vec2 posB = b.get_position();

            const float mA = a.get_mass();
            const float mB = b.get_mass();
            const float IA = a.get_inertia_moment();
            const float IB = b.get_inertia_moment();

            // Векторы от центров масс до точки контакта
            const glm::vec2 rA = contact_point - posA;
            const glm::vec2 rB = contact_point - posB;

            // 2. Расчет скоростей в точке контакта
            const glm::vec2 vA = a.velocity + glm::vec2(-a.angular_velocity * rA.y, a.angular_velocity * rA.x);
            const glm::vec2 vB = b.velocity + glm::vec2(-b.angular_velocity * rB.y, b.angular_velocity * rB.x);

            const glm::vec2 vRel = vA - vB;

            glm::vec2 n = glm::normalize(vRel);

            // Проекция относительной скорости на нормаль
            float velAlongNormal = glm::dot(vRel, n);

            const float raCrossN = rA.x * n.y - rA.y * n.x;
            const float rbCrossN = rB.x * n.y - rB.y * n.x;

            const float invMassSum = (1.0f / mA) + (1.0f / mB);
            const float invInertiaSum = (raCrossN * raCrossN) / IA + (rbCrossN * rbCrossN) / IB;

            if (invMassSum + invInertiaSum < 1e-9f) continue;

            float j = -(1.0f + restitution) * velAlongNormal;
            j /= (invMassSum + invInertiaSum);

            const glm::vec2 impulse = n * j;

            // Применяем к обоим. Важно: импульс для B равен -impulse
            a.apply_impulse_at(impulse, contact_point);
            b.apply_impulse_at(-impulse, contact_point);

            // Позиционная коррекция (чтобы не залипали)

            if (restitution > 0.1f) {
                const float correctionMag = 0.5f; // Подбери экспериментально (в единицах твоего мира)
                const glm::vec2 correction = n * correctionMag * percent;
                a.add_position(correction * (1.0f / mA) / invMassSum); // Распределение по массам
                b.add_position(-correction * (1.0f / mB) / invMassSum);
            }
            // }
        }

        for (ProjectileSlot& projSlot : projectiles_) {
            projSlot.proj->update(dt);
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
    //  Сигнатура объекта — позиция и радиус описывающей окружности
    //  в мировом пространстве. Не зависит от угла поворота.
    //
    //  Пересчитывается каждый кадр, потому что объект может двигаться.
    //  Радиус пересчитывается только если объект получил урон (дорогая
    //  операция не нужна каждый кадр — он зависит только от размера сетки).
    // -----------------------------------------------------------------------

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