#include "Editor/Workspaces/LookDev/HIKARI_LookDevArrange.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include "Editor/Workspaces/LookDev/HIKARI_LookDevPlacement.h"

namespace HIKARI::EDITOR {
namespace {

float Anchor(const Bounds& bounds, int axis, LookDevAlign align) {
    const float minimum = LookDevAxis(bounds.min, axis);
    const float maximum = LookDevAxis(bounds.max, axis);
    return align == LookDevAlign::Min ? minimum : (align == LookDevAlign::Max ? maximum : (minimum + maximum) * 0.5f);
}

} // namespace

std::vector<MATH::Vec3> AlignLookDevItems(const std::vector<LookDevArrangeItem>& items, const Bounds& reference,
                                          int axis, LookDevAlign align) {
    const float target = Anchor(reference, axis, align);
    std::vector<MATH::Vec3> moves(items.size());
    for (size_t index = 0; index < items.size(); ++index) {
        LookDevAxis(moves[index], axis) = target - Anchor(items[index].bounds, axis, align);
    }
    return moves;
}

std::vector<MATH::Vec3> DistributeLookDevItems(const std::vector<LookDevArrangeItem>& items, int axis) {
    std::vector<MATH::Vec3> moves(items.size());
    if (items.size() < 3u) {
        return moves;
    }
    std::vector<size_t> order(items.size());
    std::iota(order.begin(), order.end(), size_t{0});
    const auto center = [&](size_t index) { return Anchor(items[index].bounds, axis, LookDevAlign::Center); };
    std::sort(order.begin(), order.end(), [&](size_t lhs, size_t rhs) { return center(lhs) < center(rhs); });
    const float first = center(order.front());
    const float step = (center(order.back()) - first) / static_cast<float>(order.size() - 1u);
    for (size_t rank = 1; rank + 1u < order.size(); ++rank) {
        LookDevAxis(moves[order[rank]], axis) = first + step * static_cast<float>(rank) - center(order[rank]);
    }
    return moves;
}

std::vector<LookDevArrayCopy> BuildLookDevArray(const LookDevArraySettings& settings) {
    std::vector<LookDevArrayCopy> copies;
    const int count = std::clamp(settings.count, 1, 256);
    switch (settings.kind) {
    case LookDevArrayKind::Grid: {
        const int rows = std::clamp(settings.rows, 1, 256);
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < count; ++column) {
                if (row != 0 || column != 0) {
                    copies.push_back({{settings.spacing * static_cast<float>(column), 0.0f,
                                       settings.spacing * static_cast<float>(row)},
                                      0.0f});
                }
            }
        }
        break;
    }
    case LookDevArrayKind::Ring: {
        // 元の物は円の +X の点。中心のまわりに等角で回し、向きも同じだけ回す。
        const float step = 360.0f / static_cast<float>(count);
        for (int index = 1; index < count; ++index) {
            const float degrees = step * static_cast<float>(index);
            const float radians = degrees * 0.0174532925f;
            // Y 軸まわりの回転（+Z から +X へ）で +X の点を回す。
            const MATH::Vec3 rotated{std::cos(radians) * settings.radius, 0.0f, -std::sin(radians) * settings.radius};
            copies.push_back({{rotated.x - settings.radius, 0.0f, rotated.z}, degrees});
        }
        break;
    }
    case LookDevArrayKind::Linear:
    default:
        for (int index = 1; index < count; ++index) {
            copies.push_back({{settings.spacing * static_cast<float>(index), 0.0f, 0.0f}, 0.0f});
        }
        break;
    }
    return copies;
}

MATH::Vec3 MirrorLookDevPosition(const MATH::Vec3& position, const MATH::Vec3& center, int axis) noexcept {
    MATH::Vec3 mirrored = position;
    LookDevAxis(mirrored, axis) = 2.0f * LookDevAxis(center, axis) - LookDevAxis(position, axis);
    return mirrored;
}

MATH::Vec3 MirrorLookDevRotation(const MATH::Vec3& eulerDegrees, int axis) noexcept {
    // 鏡映 M で回転 R を写すと M R M。軸 a に垂直な面の鏡映は、a まわりの回転を保ち、他の二軸の回転を逆にする。
    MATH::Vec3 mirrored = eulerDegrees * -1.0f;
    LookDevAxis(mirrored, axis) = LookDevAxis(eulerDegrees, axis);
    return mirrored;
}

Bounds UnionLookDevBounds(const std::vector<LookDevArrangeItem>& items) noexcept {
    Bounds result{
        {(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()},
        {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
         std::numeric_limits<float>::lowest()}};
    for (const LookDevArrangeItem& item : items) {
        result.min = {(std::min)(result.min.x, item.bounds.min.x), (std::min)(result.min.y, item.bounds.min.y),
                      (std::min)(result.min.z, item.bounds.min.z)};
        result.max = {(std::max)(result.max.x, item.bounds.max.x), (std::max)(result.max.y, item.bounds.max.y),
                      (std::max)(result.max.z, item.bounds.max.z)};
    }
    return result;
}

} // namespace HIKARI::EDITOR
