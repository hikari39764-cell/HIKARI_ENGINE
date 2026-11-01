#pragma once

#include <string>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI {

enum class ReflectionProbeInfluenceShape {
    Sphere,
    Box,
};

enum class ReflectionProbeProjectionShape {
    Infinite,
    Box,
};

// 反射探針コンポーネントの作者データ。
struct ReflectionProbeSettings {
    bool enabled = false;
    std::string sourceCubemapAsset{};
    MATH::Vec3 position{0.0f, 2.0f, 0.0f};
    float radius = 8.0f;
    float intensity = 1.0f;
    // Influence は probe の有効範囲、Projection は cubemap の視差補正に使う。
    ReflectionProbeInfluenceShape influenceShape = ReflectionProbeInfluenceShape::Sphere;
    MATH::Vec3 influenceBoxCenter{0.0f, 2.0f, 0.0f};
    MATH::Vec3 influenceBoxSize{8.0f, 4.0f, 8.0f};
    ReflectionProbeProjectionShape projectionShape = ReflectionProbeProjectionShape::Infinite;
    MATH::Vec3 projectionBoxCenter{0.0f, 2.0f, 0.0f};
    MATH::Vec3 projectionBoxSize{8.0f, 4.0f, 8.0f};
    float blendDistance = 1.0f;
    int priority = 0;
};

} // namespace HIKARI

namespace HIKARI::REFLECTION {

enum class RuntimeReflectionProbeInfluenceShape {
    Sphere,
    Box,
};

enum class RuntimeReflectionProbeProjectionShape {
    Infinite,
    Box,
};

} // namespace HIKARI::REFLECTION
