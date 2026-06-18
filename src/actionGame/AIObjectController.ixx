//
// Created by AI on 16.06.2026.
//
module;

#include <glm/glm.hpp>
#include <optional>
#include <random>

export module ai_object_controller;

export import moduled_entity;
export import additional_math;

export class AIObjectController
{
private:
    ModuledEntity<VoxelObject>& object_;

    enum class AIState { Approach, Retreat, CircleStrafe };
    AIState current_state_ = AIState::Approach;

    float state_timer_ = 0.0f;
    bool strafe_left_ = true;

    float min_engage_dist_sq_ = 600.0f * 600.0f;
    float max_engage_dist_sq_ = 2000.0f * 2000.0f;

    static float random_range(float min, float max) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist(min, max);
        return dist(rng);
    }

public:
    explicit AIObjectController(ModuledEntity<VoxelObject>& obj) : object_(obj) {}

    void update(float dt, const std::optional<glm::vec2>& target)
    {
        signals::signal_t& signal = object_.signal;
        signal = 0;

        if (!target.has_value()) return;

        glm::vec2 ai_pos = object_.get_object().global_position();
        glm::vec2 target_pos = target.value();

        glm::vec2 dir = target_pos - ai_pos;
        float dist_sq = glm::dot(dir, dir);

        // поворот корпуса
        glm::vec2 forward = math::rotate_vec2({1.f, 0.f}, object_.get_object().global_rotation());

        float cross = forward.x * dir.y - forward.y * dir.x;
        float dot = glm::dot(forward, dir);

        float turn_threshold = 10.0f;

        if (cross > turn_threshold) {
            signal += signals::YAW_LEFT_SIGNAL;
        } else if (cross < -turn_threshold) {
            signal += signals::YAW_RIGHT_SIGNAL;
        }

        // состояния
        state_timer_ -= dt;

        switch (current_state_)
        {
            case AIState::Approach:
                signal += signals::FORWARD_SIGNAL;
                if (dist_sq < max_engage_dist_sq_) {
                    current_state_ = AIState::CircleStrafe;
                    state_timer_ = random_range(2.0f, 5.0f);
                    strafe_left_ = random_range(0.0f, 1.0f) > 0.5f;
                }
                break;

            case AIState::Retreat:
                signal += signals::BACKWARD_SIGNAL;
                signal += strafe_left_ ? signals::LEFT_SIGNAL : signals::RIGHT_SIGNAL;

                if (dist_sq > max_engage_dist_sq_) {
                    current_state_ = AIState::Approach;
                } else if (state_timer_ <= 0.0f) {
                    current_state_ = AIState::CircleStrafe;
                    state_timer_ = random_range(2.0f, 5.0f);
                    strafe_left_ = !strafe_left_;
                }
                break;

            case AIState::CircleStrafe:
                signal += strafe_left_ ? signals::LEFT_SIGNAL : signals::RIGHT_SIGNAL;

                // Коррекция инерции
                if (dist_sq > max_engage_dist_sq_ * 1.2f) {
                    signal += signals::FORWARD_SIGNAL;
                } else if (dist_sq < min_engage_dist_sq_ * 0.8f) {
                    signal += signals::BACKWARD_SIGNAL;
                }

                if (state_timer_ <= 0.0f) {
                    if (dist_sq < min_engage_dist_sq_) {
                        current_state_ = AIState::Retreat;
                        state_timer_ = random_range(1.0f, 2.5f);
                    } else {
                        strafe_left_ = !strafe_left_; // Просто меняем сторону стрейфа
                        state_timer_ = random_range(2.0f, 5.0f);
                    }
                }
                break;
        }

        // Стрельба
        if (dist_sq <= max_engage_dist_sq_) {
            signal += signals::PRIMAPY_WEAPON_SIGNAL;
        }
    }
};