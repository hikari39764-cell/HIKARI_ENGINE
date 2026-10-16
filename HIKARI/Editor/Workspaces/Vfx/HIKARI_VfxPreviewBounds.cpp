#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewBounds.h"

#include "Assets/Vfx/HIKARI_VfxLayerOrder.h"
#include "Render3D/Vfx/HIKARI_ParticleSpatialReference.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace HIKARI::EDITOR {
namespace {
constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

struct ScalarInterval {
    float minimum = 0.0f;
    float maximum = 0.0f;
};

struct VectorInterval {
    MATH::Vec3 minimum{};
    MATH::Vec3 maximum{};
};

struct ResolvedEmitterTransform {
    MATH::Mat4 matrix = MATH::Mat4::Identity();
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
    bool resolving = false;
    bool resolved = false;
};

float FiniteOr(float value, float fallback = 0.0f) noexcept { return std::isfinite(value) ? value : fallback; }

float Component(const MATH::Vec3& value, size_t index) noexcept {
    if (index == 0u)
        return value.x;
    if (index == 1u)
        return value.y;
    return value.z;
}

void SetComponent(MATH::Vec3& value, size_t index, float component) noexcept {
    if (index == 0u) {
        value.x = component;
    } else if (index == 1u) {
        value.y = component;
    } else {
        value.z = component;
    }
}

ScalarInterval Ordered(float lhs, float rhs) noexcept {
    lhs = FiniteOr(lhs);
    rhs = FiniteOr(rhs, lhs);
    return {(std::min)(lhs, rhs), (std::max)(lhs, rhs)};
}

ScalarInterval DistributionInterval(const VFX::VfxFloatDistributionData& value) noexcept {
    return value.mode == VFX::VfxDistributionMode::RandomRange ? Ordered(value.minimum, value.maximum)
                                                               : Ordered(value.minimum, value.minimum);
}

ScalarInterval DistributionInterval(const VFX::VfxVec3DistributionData& value, size_t component) noexcept {
    const float minimum = Component(value.minimum, component);
    const float maximum = Component(value.maximum, component);
    return value.mode == VFX::VfxDistributionMode::RandomRange ? Ordered(minimum, maximum) : Ordered(minimum, minimum);
}

ScalarInterval Merge(ScalarInterval lhs, ScalarInterval rhs) noexcept {
    return {
        (std::min)(lhs.minimum, rhs.minimum),
        (std::max)(lhs.maximum, rhs.maximum),
    };
}

ScalarInterval Add(ScalarInterval lhs, ScalarInterval rhs) noexcept {
    return {
        lhs.minimum + rhs.minimum,
        lhs.maximum + rhs.maximum,
    };
}

ScalarInterval Multiply(ScalarInterval lhs, ScalarInterval rhs) noexcept {
    const std::array products{
        lhs.minimum * rhs.minimum,
        lhs.minimum * rhs.maximum,
        lhs.maximum * rhs.minimum,
        lhs.maximum * rhs.maximum,
    };
    return {
        *std::min_element(products.begin(), products.end()),
        *std::max_element(products.begin(), products.end()),
    };
}

ScalarInterval ScaleOverLifetime(ScalarInterval value, float coefficient) noexcept {
    coefficient = (std::max)(FiniteOr(coefficient), 0.0f);
    return {
        (std::min)(0.0f, value.minimum * coefficient),
        (std::max)(0.0f, value.maximum * coefficient),
    };
}

ScalarInterval BallisticDisplacement(ScalarInterval velocity, float acceleration, float lifetime) noexcept {
    acceleration = FiniteOr(acceleration);
    lifetime = (std::max)(FiniteOr(lifetime), 0.0f);
    std::array<float, 5u> candidates{
        0.0f,
        velocity.minimum * lifetime + 0.5f * acceleration * lifetime * lifetime,
        velocity.maximum * lifetime + 0.5f * acceleration * lifetime * lifetime,
        0.0f,
        0.0f,
    };
    size_t candidateCount = 3u;
    if (std::abs(acceleration) > 1.0e-6f) {
        for (const float endpoint : {velocity.minimum, velocity.maximum}) {
            const float time = -endpoint / acceleration;
            if (time > 0.0f && time < lifetime) {
                candidates[candidateCount++] = endpoint * time + 0.5f * acceleration * time * time;
            }
        }
    }
    const auto begin = candidates.begin();
    const auto end = begin + candidateCount;
    return {
        *std::min_element(begin, end),
        *std::max_element(begin, end),
    };
}

ScalarInterval CurveInterval(const VFX::VfxCurveData& curve) noexcept {
    ScalarInterval values{};
    bool hasValue = false;
    for (const VFX::VfxCurveKeyData& key : curve.keys) {
        const float value = FiniteOr(key.value);
        if (!hasValue) {
            values = {value, value};
            hasValue = true;
        } else {
            values.minimum = (std::min)(values.minimum, value);
            values.maximum = (std::max)(values.maximum, value);
        }
    }
    const ScalarInterval offset = DistributionInterval(curve.randomOffset);
    return Add(hasValue ? values : ScalarInterval{}, offset);
}

ScalarInterval AnimatedComponentInterval(const VFX::VfxAnimatedVec3Data& value, size_t component,
                                         float lifetime) noexcept {
    switch (value.mode) {
    case VFX::VfxAnimatedValueMode::Pva: {
        ScalarInterval result = DistributionInterval(value.pva.initial, component);
        result = Add(result, ScaleOverLifetime(DistributionInterval(value.pva.velocity, component), lifetime));
        result = Add(result, ScaleOverLifetime(DistributionInterval(value.pva.acceleration, component),
                                               0.5f * lifetime * lifetime));
        return result;
    }
    case VFX::VfxAnimatedValueMode::Easing: {
        ScalarInterval result = DistributionInterval(value.easing.start, component);
        result = Merge(result, DistributionInterval(value.easing.end, component));
        if (value.easing.useMiddle) {
            result = Merge(result, DistributionInterval(value.easing.middle, component));
        }
        return result;
    }
    case VFX::VfxAnimatedValueMode::Curve:
        if (component == 0u) {
            return CurveInterval(value.curve.x);
        }
        if (component == 1u) {
            return CurveInterval(value.curve.y);
        }
        return CurveInterval(value.curve.z);
    case VFX::VfxAnimatedValueMode::Fixed:
    default: return DistributionInterval(value.fixed, component);
    }
}

VectorInterval AnimatedVectorInterval(const VFX::VfxAnimatedVec3Data& value, float lifetime) noexcept {
    VectorInterval result{};
    for (size_t component = 0u; component < 3u; ++component) {
        const ScalarInterval interval = AnimatedComponentInterval(value, component, lifetime);
        SetComponent(result.minimum, component, interval.minimum);
        SetComponent(result.maximum, component, interval.maximum);
    }
    return result;
}

VectorInterval TransformInterval(const MATH::Mat4& matrix, const VectorInterval& interval, bool vectorOnly) noexcept {
    VectorInterval result{
        {(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()},
        {(std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)(),
         (std::numeric_limits<float>::lowest)()},
    };
    for (uint32_t corner = 0u; corner < 8u; ++corner) {
        const MATH::Vec3 value{
            (corner & 1u) != 0u ? interval.maximum.x : interval.minimum.x,
            (corner & 2u) != 0u ? interval.maximum.y : interval.minimum.y,
            (corner & 4u) != 0u ? interval.maximum.z : interval.minimum.z,
        };
        const MATH::Vec3 transformed =
            vectorOnly ? MATH::TransformVector(matrix, value) : MATH::TransformPoint(matrix, value);
        result.minimum.x = (std::min)(result.minimum.x, transformed.x);
        result.minimum.y = (std::min)(result.minimum.y, transformed.y);
        result.minimum.z = (std::min)(result.minimum.z, transformed.z);
        result.maximum.x = (std::max)(result.maximum.x, transformed.x);
        result.maximum.y = (std::max)(result.maximum.y, transformed.y);
        result.maximum.z = (std::max)(result.maximum.z, transformed.z);
    }
    return result;
}

VectorInterval ShapeInterval(const VFX::VfxEmitterData& emitter) noexcept {
    const ScalarInterval radiusRange = DistributionInterval(emitter.shape.radius);
    const float radius = (std::max)(std::abs(radiusRange.minimum), std::abs(radiusRange.maximum));
    switch (emitter.shape.type) {
    case VFX::VfxShapeType::Sphere:
    case VFX::VfxShapeType::Cone:
        return {
            {-radius, -radius, -radius},
            {radius, radius, radius},
        };
    case VFX::VfxShapeType::Box: {
        const MATH::Vec3 half{
            std::abs(FiniteOr(emitter.shape.boxHalfExtents.x)),
            std::abs(FiniteOr(emitter.shape.boxHalfExtents.y)),
            std::abs(FiniteOr(emitter.shape.boxHalfExtents.z)),
        };
        return {
            {-half.x, -half.y, -half.z},
            half,
        };
    }
    case VFX::VfxShapeType::Cylinder: {
        const float halfHeight = 0.5f * std::abs(FiniteOr(emitter.shape.cylinderHeight));
        return {
            {-radius, -halfHeight, -radius},
            {radius, halfHeight, radius},
        };
    }
    case VFX::VfxShapeType::Circle:
        return {
            {-radius, 0.0f, -radius},
            {radius, 0.0f, radius},
        };
    case VFX::VfxShapeType::Edge: {
        const float halfLength = 0.5f * std::abs(FiniteOr(emitter.shape.edgeLength));
        return {
            {-halfLength, 0.0f, 0.0f},
            {halfLength, 0.0f, 0.0f},
        };
    }
    case VFX::VfxShapeType::Point:
    default: return {};
    }
}

bool ResolveTransform(size_t emitterIndex, const VFX::VfxAssetData& asset,
                      const std::unordered_map<std::string, size_t>& emitterIndices,
                      std::vector<ResolvedEmitterTransform>& transforms) {
    ResolvedEmitterTransform& result = transforms[emitterIndex];
    if (result.resolved)
        return true;
    if (result.resolving)
        return false;
    result.resolving = true;

    MATH::Mat4 parent = MATH::Mat4::Identity();
    const VFX::VfxEmitterData& emitter = asset.emitters[emitterIndex];
    if (!emitter.parentEmitterId.empty()) {
        const auto found = emitterIndices.find(emitter.parentEmitterId);
        if (found == emitterIndices.end() || !ResolveTransform(found->second, asset, emitterIndices, transforms)) {
            result.resolving = false;
            return false;
        }
        parent = transforms[found->second].matrix;
    }

    const VFX::VfxTransformData& local = emitter.emitterTransform.transform;
    const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(FiniteOr(local.rotationEulerDegrees.x) * kDegreesToRadians,
                                                         FiniteOr(local.rotationEulerDegrees.y) * kDegreesToRadians,
                                                         FiniteOr(local.rotationEulerDegrees.z) * kDegreesToRadians);
    result.matrix = parent * MATH::Mat4::TRS(local.position, rotation, local.scale);
    MATH::Quat worldRotation{};
    if (!MATH::DecomposeTRS(result.matrix, result.position, worldRotation, result.scale)) {
        result.position = MATH::TransformPoint(result.matrix, {});
        result.rotation = MATH::Quat::Identity();
        result.scale = {1.0f, 1.0f, 1.0f};
    } else {
        result.rotation = worldRotation;
    }
    result.resolving = false;
    result.resolved = true;
    return true;
}

void Expand(VectorInterval& destination, const VectorInterval& source, bool& initialized) noexcept {
    if (!initialized) {
        destination = source;
        initialized = true;
        return;
    }
    destination.minimum.x = (std::min)(destination.minimum.x, source.minimum.x);
    destination.minimum.y = (std::min)(destination.minimum.y, source.minimum.y);
    destination.minimum.z = (std::min)(destination.minimum.z, source.minimum.z);
    destination.maximum.x = (std::max)(destination.maximum.x, source.maximum.x);
    destination.maximum.y = (std::max)(destination.maximum.y, source.maximum.y);
    destination.maximum.z = (std::max)(destination.maximum.z, source.maximum.z);
}

VectorInterval RenderInterval(const VFX::VfxEmitterData& emitter, const ResolvedEmitterTransform& transform,
                              float lifetime) noexcept {
    MATH::Vec3 extent{0.5f, 0.5f, 0.5f};
    const auto& shape = emitter.renderShape;
    switch (emitter.renderShape.type) {
    case VFX::VfxRenderShapeType::MeshCylinder:
        extent.x = extent.z = (std::max)(shape.cylinderBottomRadius, shape.cylinderTopRadius);
        break;
    case VFX::VfxRenderShapeType::MeshCone: extent.x = extent.z = shape.coneBaseRadius; break;
    case VFX::VfxRenderShapeType::MeshTorus:
        extent = {shape.torusMajorRadius + shape.torusMinorRadius, shape.torusMinorRadius,
                  shape.torusMajorRadius + shape.torusMinorRadius};
        break;
    case VFX::VfxRenderShapeType::MeshCapsule: extent.x = extent.z = shape.capsuleRadius; break;
    case VFX::VfxRenderShapeType::Ring: {
        float radius = (std::max)(shape.ringInnerRadius, shape.ringOuterRadius);
        for (const auto& track : emitter.parameterTracks)
            if (track.targetId == shape.id &&
                (track.propertyId == "ringInnerRadius" || track.propertyId == "ringOuterRadius"))
                radius = (std::max)(radius, CurveInterval(track.curve).maximum);
        extent = {radius, radius, 0};
        break;
    }
    case VFX::VfxRenderShapeType::Ribbon: extent = {0.5f, 0.5f * shape.ribbonWidth, 0}; break;
    case VFX::VfxRenderShapeType::Sprite: extent.z = 0; break;
    default: break;
    }
    const float taper = (std::max)({1.0f, shape.taperStart, shape.taperEnd});
    extent.x *= taper;
    extent.z *= taper;
    extent.x += std::abs(shape.bendAmount);
    if (shape.type == VFX::VfxRenderShapeType::Ribbon || shape.type == VFX::VfxRenderShapeType::Ring)
        extent.y *= taper;
    if (std::abs(shape.twistDegrees) > 0.001f && shape.type != VFX::VfxRenderShapeType::MeshCylinder &&
        shape.type != VFX::VfxRenderShapeType::MeshCone) {
        const float r = std::sqrt(extent.x * extent.x + extent.z * extent.z);
        extent.x = extent.z = r;
    }
    VectorInterval local{};
    for (size_t component = 0; component < 3; ++component) {
        const auto scale = AnimatedComponentInterval(emitter.scale.value, component, lifetime);
        const auto interval = Multiply({-Component(extent, component) - Component(emitter.anchor, component),
                                        Component(extent, component) - Component(emitter.anchor, component)},
                                       scale);
        SetComponent(local.minimum, component, interval.minimum);
        SetComponent(local.maximum, component, interval.maximum);
    }
    const auto& rotation = emitter.rotation.eulerDegrees;
    const bool fixedRotation =
        rotation.mode == VFX::VfxAnimatedValueMode::Fixed && rotation.fixed.mode == VFX::VfxDistributionMode::Constant;
    const bool fixedShape =
        shape.billboardMode == VFX::VfxBillboardMode::Fixed || shape.type >= VFX::VfxRenderShapeType::MeshBox;
    if (fixedRotation && fixedShape) {
        const auto euler = rotation.fixed.minimum * kDegreesToRadians;
        const auto localRotation = MATH::Mat4::Rotate(MATH::Quat::FromEulerXYZ(euler.x, euler.y, euler.z));
        return TransformInterval(transform.matrix * localRotation, local, true);
    }
    // Y 軸だけを回る形状は高さとアンカーを維持する。三軸回転の
    // 球境界を使うと、接地した竜巻まで地下へ大きく拡張される。
    const auto rotationX = AnimatedComponentInterval(rotation, 0, lifetime);
    const auto rotationZ = AnimatedComponentInterval(rotation, 2, lifetime);
    // 自由接線の曲線はキー値が 0 でも途中で回転し得るため除外する。
    if (fixedShape && rotation.mode != VFX::VfxAnimatedValueMode::Curve && rotationX.minimum == 0 &&
        rotationX.maximum == 0 && rotationZ.minimum == 0 && rotationZ.maximum == 0) {
        const float x = (std::max)(std::abs(local.minimum.x), std::abs(local.maximum.x));
        const float z = (std::max)(std::abs(local.minimum.z), std::abs(local.maximum.z));
        const float radius = std::sqrt(x * x + z * z);
        local.minimum.x = local.minimum.z = -radius;
        local.maximum.x = local.maximum.z = radius;
        return TransformInterval(transform.matrix, local, true);
    }
    // 回転曲線/ビルボードのみ球で包む。静止した細長い光束を球へ
    // 拡張してから再び箱へ拡張すると、取景距離が過大になる。
    MATH::Vec3 maximum{};
    for (size_t i = 0; i < 3; ++i)
        SetComponent(maximum, i,
                     (std::max)(std::abs(Component(local.minimum, i)), std::abs(Component(local.maximum, i))));
    const float radius = MATH::Length(maximum) * (std::max)({std::abs(transform.scale.x), std::abs(transform.scale.y),
                                                             std::abs(transform.scale.z)});
    return {{-radius, -radius, -radius}, {radius, radius, radius}};
}

VectorInterval MovementInterval(const VFX::VfxEmitterData& emitter, const ResolvedEmitterTransform& transform,
                                float lifetime) noexcept {
    VectorInterval velocity{};
    const ScalarInterval speed = DistributionInterval(emitter.position.radialSpeed);
    if (emitter.shape.type == VFX::VfxShapeType::Sphere) {
        const float extent = (std::max)(std::abs(speed.minimum), std::abs(speed.maximum));
        velocity.minimum = {-extent, -extent, -extent};
        velocity.maximum = {extent, extent, extent};
    } else if (emitter.shape.type == VFX::VfxShapeType::Cone) {
        // Runtime samples inside a cone around local +Y and rotates
        // velocity without applying emitter scale. Preserve that
        // narrow lobe instead of framing it as a full velocity sphere.
        constexpr float kPi = 3.14159265358979323846f;
        const float halfAngle =
            std::clamp(std::abs(FiniteOr(emitter.shape.coneAngleDegrees)) * kDegreesToRadians * 0.5f, 0.0f, kPi);
        const float lateral = halfAngle >= kPi * 0.5f ? 1.0f : std::sin(halfAngle);
        const ScalarInterval lateralVelocity = Multiply({-lateral, lateral}, speed);
        const ScalarInterval forwardVelocity = Multiply({std::cos(halfAngle), 1.0f}, speed);
        const VectorInterval local{
            {
                lateralVelocity.minimum,
                forwardVelocity.minimum,
                lateralVelocity.minimum,
            },
            {
                lateralVelocity.maximum,
                forwardVelocity.maximum,
                lateralVelocity.maximum,
            },
        };
        const MATH::Mat4 rotationMatrix = MATH::Mat4::TRS({}, transform.rotation, {1.0f, 1.0f, 1.0f});
        velocity = TransformInterval(rotationMatrix, local, true);
    } else {
        const MATH::Mat4 rotationMatrix = MATH::Mat4::TRS({}, transform.rotation, {1.0f, 1.0f, 1.0f});
        MATH::Vec3 direction = MATH::TransformVector(rotationMatrix, {0.0f, 1.0f, 0.0f});
        const float directionLength = MATH::Length(direction);
        direction = directionLength > 1.0e-6f ? direction * (1.0f / directionLength) : MATH::Vec3{0.0f, 1.0f, 0.0f};
        for (size_t component = 0u; component < 3u; ++component) {
            const float axis = Component(direction, component);
            const ScalarInterval componentVelocity = Ordered(axis * speed.minimum, axis * speed.maximum);
            SetComponent(velocity.minimum, component, componentVelocity.minimum);
            SetComponent(velocity.maximum, component, componentVelocity.maximum);
        }
    }

    VectorInterval result{};
    for (size_t component = 0u; component < 3u; ++component) {
        const ScalarInterval displacement = BallisticDisplacement(
            {
                Component(velocity.minimum, component),
                Component(velocity.maximum, component),
            },
            Component(emitter.forces.gravity, component), lifetime);
        SetComponent(result.minimum, component, displacement.minimum);
        SetComponent(result.maximum, component, displacement.maximum);
    }
    return result;
}
} // namespace

VfxPreviewBounds EstimateVfxPreviewBounds(const VFX::VfxAssetData& asset, std::string_view emitterId,
                                          VfxPreviewBoundsPurpose purpose) {
    VfxPreviewBounds output{};
    if (asset.emitters.empty())
        return output;

    std::unordered_map<std::string, size_t> emitterIndices{};
    emitterIndices.reserve(asset.emitters.size());
    for (size_t index = 0u; index < asset.emitters.size(); ++index) {
        emitterIndices.emplace(asset.emitters[index].id, index);
    }
    std::vector<ResolvedEmitterTransform> transforms(asset.emitters.size());
    VectorInterval combined{};
    bool combinedInitialized = false;

    for (size_t index = 0u; index < asset.emitters.size(); ++index) {
        const VFX::VfxEmitterData& emitter = asset.emitters[index];
        if (emitter.kind == VFX::VfxLayerKind::Group || emitter.kind == VFX::VfxLayerKind::Adjustment)
            continue;
        if (!emitterId.empty() && emitter.id != emitterId)
            continue;
        if (!emitter.basic.enabled || !ResolveTransform(index, asset, emitterIndices, transforms)) {
            continue;
        }

        const ScalarInterval lifetimeRange = DistributionInterval(emitter.basic.lifetime);
        const bool particles = emitter.kind == VFX::VfxLayerKind::Particles;
        const float lifetime =
            particles ? (std::max)(0.0f, (std::max)(lifetimeRange.minimum, lifetimeRange.maximum))
                      : (emitter.activeDuration > 0 ? emitter.activeDuration
                                                    : (std::max)(0.0f, asset.durationSeconds - emitter.startDelay));
        const ResolvedEmitterTransform& transform = transforms[index];
        VectorInterval bounds =
            TransformInterval(transform.matrix, particles ? ShapeInterval(emitter) : VectorInterval{}, false);
        const VectorInterval spawnBounds = bounds;
        const VectorInterval movement = particles ? MovementInterval(emitter, transform, lifetime) : VectorInterval{};
        bounds.minimum = bounds.minimum + movement.minimum;
        bounds.maximum = bounds.maximum + movement.maximum;
        // フォーカスは構図を決める。方向が変わる力場を全軸へ最大加速度で
        // 広げた安全包絡は、描画範囲の推定にだけ残す。
        if (purpose == VfxPreviewBoundsPurpose::CompleteEnvelope && particles &&
            RENDER3D::PARTICLES::IsParticleForceFieldActive(emitter.forces.field)) {
            const auto& field = emitter.forces.field;
            float reach = 0.5f * lifetime * lifetime *
                          (2.0f * field.turbulence + std::abs(field.vortex) + std::abs(field.attraction));
            if (emitter.basic.localSimulation)
                reach *=
                    (std::max)({std::abs(transform.scale.x), std::abs(transform.scale.y), std::abs(transform.scale.z)});
            const MATH::Vec3 extent{reach, reach, reach};
            bounds.minimum = bounds.minimum - extent;
            bounds.maximum = bounds.maximum + extent;
        }

        // World-space floor collision prevents the ballistic fall below
        // the plane. Include rebound energy so framing still covers a
        // downward emitter after it bounces (restitution is at most one).
        if (particles && emitter.forces.collisionPlaneEnabled && !emitter.basic.localSimulation &&
            emitter.forces.gravity.y < -1.0e-5f) {
            const float floor = emitter.forces.collisionPlaneHeight;
            bounds.minimum.y = (std::min)(spawnBounds.minimum.y, (std::max)(bounds.minimum.y, floor));
            const auto speed = DistributionInterval(emitter.position.radialSpeed);
            const float maximumSpeed = (std::max)(std::abs(speed.minimum), std::abs(speed.maximum));
            const float gravity = -emitter.forces.gravity.y;
            const float reboundHeight = maximumSpeed * maximumSpeed / (2.0f * gravity);
            bounds.maximum.y = (std::max)(bounds.maximum.y, (std::max)(spawnBounds.maximum.y, floor) + reboundHeight);
        }

        const VectorInterval animatedPosition =
            TransformInterval(transform.matrix, AnimatedVectorInterval(emitter.position.value, lifetime), true);
        bounds.minimum = bounds.minimum + animatedPosition.minimum;
        bounds.maximum = bounds.maximum + animatedPosition.maximum;

        const auto renderBounds = RenderInterval(emitter, transform, lifetime);
        bounds.minimum = bounds.minimum + renderBounds.minimum;
        bounds.maximum = bounds.maximum + renderBounds.maximum;
        const auto expandSpatial = [&](const VFX::VfxEmitterData& owner) {
            const auto ownerIndex = emitterIndices.at(owner.id);
            if (!owner.basic.enabled || !ResolveTransform(ownerIndex, asset, emitterIndices, transforms))
                return;
            const auto& frame = transforms[ownerIndex];
            const auto movement = TransformInterval(
                frame.matrix, AnimatedVectorInterval(owner.position.value, asset.durationSeconds), true);
            const auto scaleRange = AnimatedVectorInterval(owner.scale.value, asset.durationSeconds);
            const MATH::Vec3 origin = frame.position + (movement.minimum + movement.maximum) * .5f;
            const float originRadius = MATH::Length(movement.maximum - movement.minimum) * .5f;
            float minimumScale = std::numeric_limits<float>::max(), maximumScale = 0;
            for (size_t axis = 0; axis < 3; ++axis) {
                const float lo = Component(scaleRange.minimum, axis), hi = Component(scaleRange.maximum, axis);
                const float base = std::abs(Component(frame.scale, axis));
                minimumScale =
                    (std::min)(minimumScale, base * (lo <= 0 && hi >= 0 ? 0 : (std::min)(std::abs(lo), std::abs(hi))));
                maximumScale = (std::max)(maximumScale, base * (std::max)(std::abs(lo), std::abs(hi)));
            }
            minimumScale = (std::max)(minimumScale, .0001f);
            maximumScale = (std::max)(maximumScale, .0001f);
            for (const auto& effect : owner.effects) {
                if (!effect.enabled || effect.space != MaterialEffectSpace::Group3D)
                    continue;
                auto parameters = effect.parameters;
                // 変形の最大変位に関係する成分だけ、曲線の契約範囲で包む。
                float* values[]{&parameters.x, &parameters.y, &parameters.z, &parameters.w};
                const auto info = GetMaterialEffectParameters(effect);
                for (const auto& track : owner.parameterTracks) {
                    if (track.targetId != effect.id || !track.propertyId.starts_with("parameters."))
                        continue;
                    const auto axis = std::string_view("xyzw").find(track.propertyId.back());
                    if (axis >= 4)
                        continue;
                    const auto range = CurveInterval(track.curve);
                    float lo = (std::max)(info[axis].minimum, range.minimum),
                          hi = (std::min)(info[axis].maximum, range.maximum);
                    if (track.curve.interpolation == VFX::VfxCurveInterpolation::Smooth) {
                        lo = info[axis].minimum;
                        hi = info[axis].maximum;
                    }
                    const bool center = (effect.kind == MaterialEffectKind::Polar && (axis == 1 || axis == 2)) ||
                                        (effect.kind == MaterialEffectKind::Twirl && axis >= 2);
                    const float pivot = center ? .5f : 0;
                    const float extreme = std::abs(lo - pivot) > std::abs(hi - pivot) ? lo : hi;
                    *values[axis] =
                        effect.kind == MaterialEffectKind::Polar && axis == 3
                            ? (std::min)(*values[axis], lo)
                            : (std::abs(extreme - pivot) > std::abs(*values[axis] - pivot) ? extreme : *values[axis]);
                }
                const auto center = (bounds.minimum + bounds.maximum) * .5f;
                const float radius =
                    (MATH::Length(center - origin) + MATH::Length(bounds.maximum - center) + originRadius) /
                    minimumScale;
                const float expanded = (radius + RENDER3D::PARTICLES::ParticleSpatialBoundExpansion(
                                                     uint32_t(effect.kind), parameters, effect.parameters2)) *
                                           maximumScale +
                                       originRadius;
                const MATH::Vec3 extent{expanded, expanded, expanded};
                bounds = {origin - extent, origin + extent};
            }
        };
        expandSpatial(emitter);
        for (const auto* owner : VFX::CollectVfxAdjustments(asset.emitters, emitter))
            expandSpatial(*owner);
        Expand(combined, bounds, combinedInitialized);
    }

    if (!combinedInitialized)
        return output;
    output.valid = true;
    output.minimum = combined.minimum;
    output.maximum = combined.maximum;
    output.center = (combined.minimum + combined.maximum) * 0.5f;
    output.radius = MATH::Length(combined.maximum - output.center);
    if (!std::isfinite(output.radius)) {
        return {};
    }
    return output;
}

} // namespace HIKARI::EDITOR
