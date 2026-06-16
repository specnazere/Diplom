//
// Created by AI on 16.06.2026.
//
module;

#include <glm/glm.hpp>
#include <optional>
#include <random>

export module ai_object_controller;

import destructible_entity;
import additional_math;

export class AIObjectController
{
private:
    ModuledEntity& object_;

    enum class AIState { Approach, Retreat, CircleStrafe };
    AIState currentState_ = AIState::Approach;

    float stateTimer_ = 0.0f;
    bool strafeLeft_ = true;

    float minEngageDistSq_ = 600.0f * 600.0f;
    float maxEngageDistSq_ = 2000.0f * 2000.0f;

    // Вспомогалка для рандома, замени на свой engine
    static float random_range(float min, float max) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist(min, max);
        return dist(rng);
    }

public:
    explicit AIObjectController(ModuledEntity& obj) : object_(obj) {}

    void update(float dt, const std::optional<glm::vec2>& target)
    {
        signals::signal_t& signal = object_.signal;
        signal = 0;

        if (!target.has_value()) return;

        glm::vec2 ai_pos = object_.get_object().global_position();
        glm::vec2 target_pos = target.value();

        glm::vec2 dir = target_pos - ai_pos;
        float dist_sq = glm::dot(dir, dir);

        // --- 1. Прицеливание (Поворот корпуса) ---
        glm::vec2 forward = math::rotate_vec2({1.f, 0.f}, object_.get_object().global_rotation());

        float cross = forward.x * dir.y - forward.y * dir.x;
        float dot = glm::dot(forward, dir);

        float turn_threshold = 10.0f;

        if (cross > turn_threshold) {
            signal += signals::yawLeftSignal;
        } else if (cross < -turn_threshold) {
            signal += signals::yawRightSignal;
        }

        // --- 2. Машина состояний (FSM) ---
        stateTimer_ -= dt;

        switch (currentState_)
        {
            case AIState::Approach:
                signal += signals::forwardSignal;
                if (dist_sq < maxEngageDistSq_) {
                    currentState_ = AIState::CircleStrafe;
                    stateTimer_ = random_range(2.0f, 5.0f);
                    strafeLeft_ = random_range(0.0f, 1.0f) > 0.5f;
                }
                break;

            case AIState::Retreat:
                signal += signals::backwardSignal;
                signal += strafeLeft_ ? signals::leftSignal : signals::rightSignal;

                if (dist_sq > maxEngageDistSq_) {
                    currentState_ = AIState::Approach;
                } else if (stateTimer_ <= 0.0f) {
                    currentState_ = AIState::CircleStrafe;
                    stateTimer_ = random_range(2.0f, 5.0f);
                    strafeLeft_ = !strafeLeft_;
                }
                break;

            case AIState::CircleStrafe:
                signal += strafeLeft_ ? signals::leftSignal : signals::rightSignal;

                // Коррекция инерции. Раз нет торможения, бота будет сносить.
                // Подгазовываем или тормозим двигателем, чтобы держать орбиту.
                if (dist_sq > maxEngageDistSq_ * 1.2f) {
                    signal += signals::forwardSignal;
                } else if (dist_sq < minEngageDistSq_ * 0.8f) {
                    signal += signals::backwardSignal;
                }

                if (stateTimer_ <= 0.0f) {
                    if (dist_sq < minEngageDistSq_) {
                        currentState_ = AIState::Retreat;
                        stateTimer_ = random_range(1.0f, 2.5f);
                    } else {
                        strafeLeft_ = !strafeLeft_; // Просто меняем сторону стрейфа
                        stateTimer_ = random_range(2.0f, 5.0f);
                    }
                }
                break;
        }

        // --- 3. Стрельба ---
        // Шлём сигнал стрельбы. Кулдаун внутри объекта.
        // Условие: цель в радиусе И мы смотрим примерно в её сторону (dot > 0.8).
        if (dist_sq <= maxEngageDistSq_) {
            signal += signals::primaryWeaponSignal;
        }
    }
};