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

VerticalMove moveVertically(const Rect& body, float distance, const Rect& bounds,
                           const std::vector<Rect>& platforms)
{
    const float targetY = body.position.y + distance;
    float nextY = targetY;
    bool blocked = false;
    for (const Rect& platform : platforms) {
        if (body.position.x + body.size.x <= platform.position.x ||
            body.position.x >= platform.position.x + platform.size.x) {
            continue;
        }
        if (distance > 0.0f && body.position.y + body.size.y <= platform.position.y &&
            nextY + body.size.y >= platform.position.y) {
            nextY = platform.position.y - body.size.y;
            blocked = true;
        } else if (distance < 0.0f &&
                   body.position.y >= platform.position.y + platform.size.y &&
                   nextY <= platform.position.y + platform.size.y) {
            nextY = platform.position.y + platform.size.y;
            blocked = true;
        }
    }
    const float maxY = std::max(bounds.position.y,
                               bounds.position.y + bounds.size.y - body.size.y);
    if ((distance < 0.0f && nextY <= bounds.position.y) ||
        (distance > 0.0f && nextY >= maxY)) {
        blocked = true;
    }
    return {std::clamp(nextY, bounds.position.y, maxY), blocked};
}

bool isSupported(const Rect& body, const Rect& bounds, const std::vector<Rect>& platforms)
{
    return moveVertically(body, 1.0f, bounds, platforms).y == body.position.y;
}

} // namespace cubeshot::collision
