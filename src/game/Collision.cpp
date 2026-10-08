#include "game/Collision.h"

#include <algorithm>

namespace cubeshot::collision {

bool overlaps(const Rect& first, const Rect& second)
{
    return first.position.x < second.position.x + second.size.x &&
           first.position.x + first.size.x > second.position.x &&
           first.position.y < second.position.y + second.size.y &&
           first.position.y + first.size.y > second.position.y;
}

float moveHorizontally(const Rect& body, float distance, const Rect& bounds,
                       const std::vector<Rect>& platforms)
{
    float nextX = body.position.x + distance;
    for (const Rect& platform : platforms) {
        if (body.position.y + body.size.y <= platform.position.y ||
            body.position.y >= platform.position.y + platform.size.y) {
            continue;
        }

        // Проверяем весь путь, чтобы не проскочить тонкую стену за один шаг.
        if (distance > 0.0f && body.position.x + body.size.x <= platform.position.x) {
            nextX = std::min(nextX, platform.position.x - body.size.x);
        } else if (distance < 0.0f &&
                   body.position.x >= platform.position.x + platform.size.x) {
            nextX = std::max(nextX, platform.position.x + platform.size.x);
        }
    }

    const float maxX = std::max(bounds.position.x,
                               bounds.position.x + bounds.size.x - body.size.x);
    return std::clamp(nextX, bounds.position.x, maxX);
}

} // namespace cubeshot::collision
