#pragma once

#include "game/Types.h"

#include <vector>
#include <optional>

namespace cubeshot::collision {

bool overlaps(const Rect& first, const Rect& second);

// Первое касание отрезка с прямоугольником, расширенным на halfSize.
// Возвращает долю пути [0, 1]; проверяет весь путь быстрой пули.
std::optional<float> segmentHit(Vec2 start, Vec2 end, const Rect& rectangle,
                                float halfSize = 0.0f);

// Возвращает X после движения, ограниченного ареной и боками платформ.
// Начальное положение должно быть свободно от пересечений.
float moveHorizontally(const Rect& body, float distance, const Rect& bounds,
                       const std::vector<Rect>& platforms);

struct VerticalMove {
    float y;
    bool blocked;
};

// Проверяет весь вертикальный путь, включая верх и низ платформ.
VerticalMove moveVertically(const Rect& body, float distance, const Rect& bounds,
                           const std::vector<Rect>& platforms);
bool isSupported(const Rect& body, const Rect& bounds, const std::vector<Rect>& platforms);

} // namespace cubeshot::collision
