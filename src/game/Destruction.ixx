//
// Created by A on 22.04.2026.
//

module;

#include <cmath>
#include <glm/vec2.hpp>

#include <functional>
#include <vector>

export module destruction;

export
namespace destruction
{
    struct epicenter
    {
        glm::vec2 position;
        float radius;
        float power;
        float heritability;
    };

    class Destruction
    {
    public:
        virtual ~Destruction() = default;

        epicenter* primaryEpicenter = nullptr;

        virtual std::vector<epicenter*> newFollowings(epicenter*) = 0;
    };

    class DestructionRailgun : public Destruction
    {
    public:
        glm::vec2 dPos;
        float count;

        DestructionRailgun(const float& size, const float& power, const glm::vec2& position, const float& angle)
        {
            primaryEpicenter = new epicenter();
            primaryEpicenter->power = power;
            primaryEpicenter->position = position;
            primaryEpicenter->heritability = 1;
            primaryEpicenter->radius = size;

            count = float(0);
            dPos = glm::vec2(std::sin(angle) * size, std::cos(angle) * size);
        }

        std::vector<epicenter*> newFollowings(epicenter* primary) override
        {
            if (primary->power == 0)
                return std::vector<epicenter*>();

            auto secondary = new epicenter();
            float x = primary->position.x + dPos.x * count;
            float y = primary->position.y + dPos.y * count;

            secondary->position = glm::vec2(x, y);
            secondary->power = primary->power - 1;
            secondary->radius = primary->radius;
            count++;

            std::vector<epicenter*> epicenters;
            epicenters.push_back(secondary);
            return epicenters;
        }
    };
}
