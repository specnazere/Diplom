//
// Created by A on 22.04.2026.
//

module;

#include <vector>

export module spacehip;

import destruction;

#define MIN_POWER_THRESHOLD 0
#define MAX_RECURSION_DEPTH 10

export
class Spaceship
{
private:

public:
    explicit Spaceship()
    {
    }

    void applyDestruction(destruction::Destruction& destruction)
    {
        std::vector<destruction::epicenter*> currentEpicenters;
        currentEpicenters.push_back(destruction.primaryEpicenter);

        std::vector<destruction::epicenter*> nextEpicenters;
        while (!currentEpicenters.empty()) {
            for (destruction::epicenter* epicenter : currentEpicenters) {

                // 2. Применяем физику к мешу
                // Сначала делим, чтобы создать вершины для излома
                //mesh_.subdivide_in_radius(epicenter->position, epicenter->radius * 3);

                // Потом раздвигаем
                //mesh_.displace_vertices(epicenter->position, epicenter->power * 3, epicenter->radius * 2);

                // 3. Генерация новых эпицентров
                // Важно: newFollowings может вернуть пустой вектор
                nextEpicenters = destruction.newFollowings(epicenter); // или как там у тебя передается контекст
            }
            currentEpicenters.clear();
            currentEpicenters = std::move(nextEpicenters);
            nextEpicenters.clear();
        }
    }

    void addToDraw()
    {
    }
};