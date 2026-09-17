#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewBounds.h"

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
        constexpr float kDegreesToRadians =
            3.14159265358979323846f / 180.0f;

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
            MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
            bool resolving = false;
            bool resolved = false;
        };

        float FiniteOr(float value, float fallback = 0.0f) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        float Component(const MATH::Vec3& value, size_t index) noexcept {
            if (index == 0u) return value.x;
            if (index == 1u) return value.y;
            return value.z;
        }

        void SetComponent(
            MATH::Vec3& value,
            size_t index,
            float component) noexcept {

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
            return { (std::min)(lhs, rhs), (std::max)(lhs, rhs) };
        }

        ScalarInterval DistributionInterval(
            const VFX::VfxFloatDistributionData& value) noexcept {

            return value.mode == VFX::VfxDistributionMode::RandomRange
                ? Ordered(value.minimum, value.maximum)
                : Ordered(value.minimum, value.minimum);
        }

        ScalarInterval DistributionInterval(
            const VFX::VfxVec3DistributionData& value,
            size_t component) noexcept {

            const float minimum = Component(value.minimum, component);
            const float maximum = Component(value.maximum, component);
            return value.mode == VFX::VfxDistributionMode::RandomRange
                ? Ordered(minimum, maximum)
                : Ordered(minimum, minimum);
        }

        ScalarInterval Merge(
            ScalarInterval lhs,
            ScalarInterval rhs) noexcept {

            return {
                (std::min)(lhs.minimum, rhs.minimum),
                (std::max)(lhs.maximum, rhs.maximum),
            };
        }

        ScalarInterval Add(
            ScalarInterval lhs,
            ScalarInterval rhs) noexcept {

            return {
                lhs.minimum + rhs.minimum,
                lhs.maximum + rhs.maximum,
            };
        }

        ScalarInterval Multiply(
            ScalarInterval lhs,
            ScalarInterval rhs) noexcept {

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

        ScalarInterval ScaleOverLifetime(
            ScalarInterval value,
            float coefficient) noexcept {

            coefficient = (std::max)(FiniteOr(coefficient), 0.0f);
            return {
                (std::min)(0.0f, value.minimum * coefficient),
                (std::max)(0.0f, value.maximum * coefficient),
            };
        }

        ScalarInterval BallisticDisplacement(
            ScalarInterval velocity,
            float acceleration,
            float lifetime) noexcept {

            acceleration = FiniteOr(acceleration);
            lifetime = (std::max)(FiniteOr(lifetime), 0.0f);
            std::array<float, 5u> candidates{
                0.0f,
                velocity.minimum * lifetime +
                    0.5f * acceleration * lifetime * lifetime,
                velocity.maximum * lifetime +
                    0.5f * acceleration * lifetime * lifetime,
                0.0f,
                0.0f,
            };
            size_t candidateCount = 3u;
            if (std::abs(acceleration) > 1.0e-6f) {
                for (const float endpoint :
                     { velocity.minimum, velocity.maximum }) {
                    const float time = -endpoint / acceleration;
                    if (time > 0.0f && time < lifetime) {
                        candidates[candidateCount++] =
                            endpoint * time +
                            0.5f * acceleration * time * time;
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

        ScalarInterval CurveInterval(
            const VFX::VfxCurveData& curve) noexcept {

            ScalarInterval values{};
            bool hasValue = false;
            for (const VFX::VfxCurveKeyData& key : curve.keys) {
                const float value = FiniteOr(key.value);
                if (!hasValue) {
                    values = { value, value };
                    hasValue = true;
                } else {
                    values.minimum = (std::min)(values.minimum, value);
                    values.maximum = (std::max)(values.maximum, value);
                }
            }
            const ScalarInterval offset =
                DistributionInterval(curve.randomOffset);
            return Add(hasValue ? values : ScalarInterval{}, offset);
        }

        ScalarInterval AnimatedComponentInterval(
            const VFX::VfxAnimatedVec3Data& value,
            size_t component,
            float lifetime) noexcept {

            switch (value.mode) {
            case VFX::VfxAnimatedValueMode::Pva: {
                ScalarInterval result = DistributionInterval(
                    value.pva.initial, component);
                result = Add(
                    result,
                    ScaleOverLifetime(
                        DistributionInterval(
                            value.pva.velocity, component),
                        lifetime));
                result = Add(
                    result,
                    ScaleOverLifetime(
                        DistributionInterval(
                            value.pva.acceleration, component),
                        0.5f * lifetime * lifetime));
                return result;
            }
            case VFX::VfxAnimatedValueMode::Easing: {
                ScalarInterval result = DistributionInterval(
                    value.easing.start, component);
                result = Merge(
                    result,
                    DistributionInterval(
                        value.easing.end, component));
                if (value.easing.useMiddle) {
                    result = Merge(
                        result,
                        DistributionInterval(
                            value.easing.middle, component));
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
            default:
                return DistributionInterval(value.fixed, component);
            }
        }

        VectorInterval AnimatedVectorInterval(
            const VFX::VfxAnimatedVec3Data& value,
            float lifetime) noexcept {

            VectorInterval result{};
            for (size_t component = 0u; component < 3u; ++component) {
                const ScalarInterval interval = AnimatedComponentInterval(
                    value, component, lifetime);
                SetComponent(result.minimum, component, interval.minimum);
                SetComponent(result.maximum, component, interval.maximum);
            }
            return result;
        }

        MATH::Vec3 TransformPoint(
            const MATH::Mat4& matrix,
            const MATH::Vec3& value) noexcept {

            const MATH::Vec4 transformed = matrix.TransformPoint(
                { value.x, value.y, value.z, 1.0f });
            return { transformed.x, transformed.y, transformed.z };
        }

        MATH::Vec3 TransformVector(
            const MATH::Mat4& matrix,
            const MATH::Vec3& value) noexcept {

            const MATH::Vec4 transformed = matrix.TransformPoint(
                { value.x, value.y, value.z, 0.0f });
            return { transformed.x, transformed.y, transformed.z };
        }

        VectorInterval TransformInterval(
            const MATH::Mat4& matrix,
            const VectorInterval& interval,
            bool vectorOnly) noexcept {

            VectorInterval result{
                { (std::numeric_limits<float>::max)(),
                  (std::numeric_limits<float>::max)(),
                  (std::numeric_limits<float>::max)() },
                { (std::numeric_limits<float>::lowest)(),
                  (std::numeric_limits<float>::lowest)(),
                  (std::numeric_limits<float>::lowest)() },
            };
            for (uint32_t corner = 0u; corner < 8u; ++corner) {
                const MATH::Vec3 value{
                    (corner & 1u) != 0u
                        ? interval.maximum.x : interval.minimum.x,
                    (corner & 2u) != 0u
                        ? interval.maximum.y : interval.minimum.y,
                    (corner & 4u) != 0u
                        ? interval.maximum.z : interval.minimum.z,
                };
                const MATH::Vec3 transformed = vectorOnly
                    ? TransformVector(matrix, value)
                    : TransformPoint(matrix, value);
                result.minimum.x = (std::min)(
                    result.minimum.x, transformed.x);
                result.minimum.y = (std::min)(
                    result.minimum.y, transformed.y);
                result.minimum.z = (std::min)(
                    result.minimum.z, transformed.z);
                result.maximum.x = (std::max)(
                    result.maximum.x, transformed.x);
                result.maximum.y = (std::max)(
                    result.maximum.y, transformed.y);
                result.maximum.z = (std::max)(
                    result.maximum.z, transformed.z);
            }
            return result;
        }

        VectorInterval ShapeInterval(
            const VFX::VfxEmitterData& emitter) noexcept {

            const ScalarInterval radiusRange =
                DistributionInterval(emitter.shape.radius);
            const float radius = (std::max)(
                std::abs(radiusRange.minimum),
                std::abs(radiusRange.maximum));
            switch (emitter.shape.type) {
            case VFX::VfxShapeType::Sphere:
            case VFX::VfxShapeType::Cone:
                return {
                    { -radius, -radius, -radius },
                    { radius, radius, radius },
                };
            case VFX::VfxShapeType::Box: {
                const MATH::Vec3 half{
                    std::abs(FiniteOr(emitter.shape.boxHalfExtents.x)),
                    std::abs(FiniteOr(emitter.shape.boxHalfExtents.y)),
                    std::abs(FiniteOr(emitter.shape.boxHalfExtents.z)),
                };
                return {
                    { -half.x, -half.y, -half.z },
                    half,
                };
            }
            case VFX::VfxShapeType::Cylinder: {
                const float halfHeight = 0.5f * std::abs(
                    FiniteOr(emitter.shape.cylinderHeight));
                return {
                    { -radius, -halfHeight, -radius },
                    { radius, halfHeight, radius },
                };
            }
            case VFX::VfxShapeType::Circle:
                return {
                    { -radius, 0.0f, -radius },
                    { radius, 0.0f, radius },
                };
            case VFX::VfxShapeType::Edge: {
                const float halfLength = 0.5f * std::abs(
                    FiniteOr(emitter.shape.edgeLength));
                return {
                    { -halfLength, 0.0f, 0.0f },
                    { halfLength, 0.0f, 0.0f },
                };
            }
            case VFX::VfxShapeType::Point:
            default:
                return {};
            }
        }

        bool ResolveTransform(
            size_t emitterIndex,
            const VFX::VfxAssetData& asset,
            const std::unordered_map<std::string, size_t>& emitterIndices,
            std::vector<ResolvedEmitterTransform>& transforms) {

            ResolvedEmitterTransform& result = transforms[emitterIndex];
            if (result.resolved) return true;
            if (result.resolving) return false;
            result.resolving = true;

            MATH::Mat4 parent = MATH::Mat4::Identity();
            const VFX::VfxEmitterData& emitter =
                asset.emitters[emitterIndex];
            if (!emitter.parentEmitterId.empty()) {
                const auto found = emitterIndices.find(
                    emitter.parentEmitterId);
                if (found == emitterIndices.end() ||
                    !ResolveTransform(
                        found->second,
                        asset,
                        emitterIndices,
                        transforms)) {
                    result.resolving = false;
                    return false;
                }
                parent = transforms[found->second].matrix;
            }

            const VFX::VfxTransformData& local =
                emitter.emitterTransform.transform;
            const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
                FiniteOr(local.rotationEulerDegrees.x) *
                    kDegreesToRadians,
                FiniteOr(local.rotationEulerDegrees.y) *
                    kDegreesToRadians,
                FiniteOr(local.rotationEulerDegrees.z) *
                    kDegreesToRadians);
            result.matrix = parent * MATH::Mat4::TRS(
                local.position,
                rotation,
                local.scale);
            MATH::Quat worldRotation{};
            if (!MATH::DecomposeTRS(
                    result.matrix,
                    result.position,
                    worldRotation,
                    result.scale)) {
                result.position = TransformPoint(
                    result.matrix, {});
                result.rotation = MATH::Quat::Identity();
                result.scale = { 1.0f, 1.0f, 1.0f };
            } else {
                result.rotation = worldRotation;
            }
            result.resolving = false;
            result.resolved = true;
            return true;
        }

        void Expand(
            VectorInterval& destination,
            const VectorInterval& source,
            bool& initialized) noexcept {

            if (!initialized) {
                destination = source;
                initialized = true;
                return;
            }
            destination.minimum.x = (std::min)(
                destination.minimum.x, source.minimum.x);
            destination.minimum.y = (std::min)(
                destination.minimum.y, source.minimum.y);
            destination.minimum.z = (std::min)(
                destination.minimum.z, source.minimum.z);
            destination.maximum.x = (std::max)(
                destination.maximum.x, source.maximum.x);
            destination.maximum.y = (std::max)(
                destination.maximum.y, source.maximum.y);
            destination.maximum.z = (std::max)(
                destination.maximum.z, source.maximum.z);
        }

        float MaximumAnimatedMagnitude(
            const VFX::VfxAnimatedVec3Data& value,
            size_t component,
            float lifetime) noexcept {

            const ScalarInterval interval = AnimatedComponentInterval(
                value, component, lifetime);
            return (std::max)(
                std::abs(interval.minimum),
                std::abs(interval.maximum));
        }

        float RenderRadius(
            const VFX::VfxEmitterData& emitter,
            const MATH::Vec3& worldScale,
            float lifetime) noexcept {

            const float scaleX = MaximumAnimatedMagnitude(
                emitter.scale.value, 0u, lifetime) *
                std::abs(FiniteOr(worldScale.x, 1.0f));
            const float scaleY = MaximumAnimatedMagnitude(
                emitter.scale.value, 1u, lifetime) *
                std::abs(FiniteOr(worldScale.y, 1.0f));
            const float scaleZ = MaximumAnimatedMagnitude(
                emitter.scale.value, 2u, lifetime) * std::abs(FiniteOr(worldScale.z, 1.0f));
            switch (emitter.renderShape.type) {
            case VFX::VfxRenderShapeType::MeshBox:
            case VFX::VfxRenderShapeType::MeshSphere:
            case VFX::VfxRenderShapeType::MeshCylinder:
            case VFX::VfxRenderShapeType::MeshCone:
            case VFX::VfxRenderShapeType::MeshHemisphere:
            case VFX::VfxRenderShapeType::CrossPlane:
                return 0.5f * std::sqrt(scaleX*scaleX+scaleY*scaleY+scaleZ*scaleZ);
            case VFX::VfxRenderShapeType::Ribbon: {
                const float halfLength = 0.5f * scaleX;
                const float halfWidth = 0.5f * std::abs(
                    FiniteOr(emitter.renderShape.ribbonWidth)) * scaleY;
                return std::sqrt(
                    halfLength * halfLength + halfWidth * halfWidth);
            }
            case VFX::VfxRenderShapeType::Ring:
                return std::abs(FiniteOr(
                    emitter.renderShape.ringOuterRadius)) *
                    (std::max)(scaleX, scaleY);
            case VFX::VfxRenderShapeType::Sprite:
            default:
                return 0.5f * std::sqrt(
                    scaleX * scaleX + scaleY * scaleY);
            }
        }

        VectorInterval MovementInterval(
            const VFX::VfxEmitterData& emitter,
            const ResolvedEmitterTransform& transform,
            float lifetime) noexcept {

            VectorInterval velocity{};
            const ScalarInterval speed =
                DistributionInterval(emitter.position.radialSpeed);
            if (emitter.shape.type == VFX::VfxShapeType::Sphere) {
                const float extent = (std::max)(
                    std::abs(speed.minimum),
                    std::abs(speed.maximum));
                velocity.minimum = { -extent, -extent, -extent };
                velocity.maximum = { extent, extent, extent };
            } else if (emitter.shape.type == VFX::VfxShapeType::Cone) {
                // Runtime samples inside a cone around local +Y and rotates
                // velocity without applying emitter scale. Preserve that
                // narrow lobe instead of framing it as a full velocity sphere.
                constexpr float kPi = 3.14159265358979323846f;
                const float halfAngle = std::clamp(
                    std::abs(FiniteOr(
                        emitter.shape.coneAngleDegrees)) *
                        kDegreesToRadians * 0.5f,
                    0.0f,
                    kPi);
                const float lateral = halfAngle >= kPi * 0.5f
                    ? 1.0f
                    : std::sin(halfAngle);
                const ScalarInterval lateralVelocity =
                    Multiply({ -lateral, lateral }, speed);
                const ScalarInterval forwardVelocity =
                    Multiply({ std::cos(halfAngle), 1.0f }, speed);
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
                const MATH::Mat4 rotationMatrix = MATH::Mat4::TRS(
                    {},
                    transform.rotation,
                    { 1.0f, 1.0f, 1.0f });
                velocity = TransformInterval(
                    rotationMatrix,
                    local,
                    true);
            } else {
                const MATH::Mat4 rotationMatrix = MATH::Mat4::TRS(
                    {},
                    transform.rotation,
                    { 1.0f, 1.0f, 1.0f });
                MATH::Vec3 direction = TransformVector(
                    rotationMatrix, { 0.0f, 1.0f, 0.0f });
                const float directionLength = MATH::Length(direction);
                direction = directionLength > 1.0e-6f
                    ? direction * (1.0f / directionLength)
                    : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
                for (size_t component = 0u; component < 3u; ++component) {
                    const float axis = Component(direction, component);
                    const ScalarInterval componentVelocity = Ordered(
                        axis * speed.minimum,
                        axis * speed.maximum);
                    SetComponent(
                        velocity.minimum,
                        component,
                        componentVelocity.minimum);
                    SetComponent(
                        velocity.maximum,
                        component,
                        componentVelocity.maximum);
                }
            }

            VectorInterval result{};
            for (size_t component = 0u; component < 3u; ++component) {
                const ScalarInterval displacement = BallisticDisplacement(
                    {
                        Component(velocity.minimum, component),
                        Component(velocity.maximum, component),
                    },
                    Component(emitter.forces.gravity, component),
                    lifetime);
                SetComponent(
                    result.minimum,
                    component,
                    displacement.minimum);
                SetComponent(
                    result.maximum,
                    component,
                    displacement.maximum);
            }
            return result;
        }
    }

    VfxPreviewBounds EstimateVfxPreviewBounds(
        const VFX::VfxAssetData& asset,
        std::string_view emitterId) {

        VfxPreviewBounds output{};
        if (asset.emitters.empty()) return output;

        std::unordered_map<std::string, size_t> emitterIndices{};
        emitterIndices.reserve(asset.emitters.size());
        for (size_t index = 0u; index < asset.emitters.size(); ++index) {
            emitterIndices.emplace(asset.emitters[index].id, index);
        }
        std::vector<ResolvedEmitterTransform> transforms(
            asset.emitters.size());
        VectorInterval combined{};
        bool combinedInitialized = false;

        for (size_t index = 0u; index < asset.emitters.size(); ++index) {
            const VFX::VfxEmitterData& emitter = asset.emitters[index];
            if (emitter.kind == VFX::VfxLayerKind::Group) continue;
            if (!emitterId.empty() && emitter.id != emitterId) continue;
            if (!emitter.basic.enabled ||
                !ResolveTransform(
                    index, asset, emitterIndices, transforms)) {
                continue;
            }

            const ScalarInterval lifetimeRange =
                DistributionInterval(emitter.basic.lifetime);
            const bool particles = emitter.kind == VFX::VfxLayerKind::Particles;
            const float lifetime = particles ? (std::max)(
                0.0f,
                (std::max)(lifetimeRange.minimum, lifetimeRange.maximum)) :
                (emitter.activeDuration > 0 ? emitter.activeDuration : (std::max)(0.0f, asset.durationSeconds - emitter.startDelay));
            const ResolvedEmitterTransform& transform = transforms[index];
            VectorInterval bounds = TransformInterval(
                transform.matrix,
                particles ? ShapeInterval(emitter) : VectorInterval{},
                false);
            const VectorInterval spawnBounds = bounds;
            const VectorInterval movement = particles ? MovementInterval(
                emitter, transform, lifetime) : VectorInterval{};
            bounds.minimum = bounds.minimum + movement.minimum;
            bounds.maximum = bounds.maximum + movement.maximum;

            // World-space floor collision prevents the ballistic fall below
            // the plane. Include rebound energy so framing still covers a
            // downward emitter after it bounces (restitution is at most one).
            if (particles && emitter.forces.collisionPlaneEnabled &&
                !emitter.basic.localSimulation && emitter.forces.gravity.y < -1.0e-5f) {
                const float floor = emitter.forces.collisionPlaneHeight;
                bounds.minimum.y = (std::min)(spawnBounds.minimum.y,
                    (std::max)(bounds.minimum.y, floor));
                const auto speed = DistributionInterval(emitter.position.radialSpeed);
                const float maximumSpeed = (std::max)(std::abs(speed.minimum), std::abs(speed.maximum));
                const float gravity = -emitter.forces.gravity.y;
                const float reboundHeight = maximumSpeed * maximumSpeed / (2.0f * gravity);
                bounds.maximum.y = (std::max)(bounds.maximum.y,
                    (std::max)(spawnBounds.maximum.y, floor) + reboundHeight);
            }

            const VectorInterval animatedPosition = TransformInterval(
                transform.matrix,
                AnimatedVectorInterval(
                    emitter.position.value, lifetime),
                true);
            bounds.minimum = bounds.minimum + animatedPosition.minimum;
            bounds.maximum = bounds.maximum + animatedPosition.maximum;

            const float renderRadius = RenderRadius(
                emitter, transform.scale, lifetime);
            const MATH::Vec3 renderExtent{
                renderRadius, renderRadius, renderRadius
            };
            bounds.minimum = bounds.minimum - renderExtent;
            bounds.maximum = bounds.maximum + renderExtent;
            Expand(combined, bounds, combinedInitialized);
        }

        if (!combinedInitialized) return output;
        output.valid = true;
        output.minimum = combined.minimum;
        output.maximum = combined.maximum;
        output.center = (combined.minimum + combined.maximum) * 0.5f;
        output.radius = MATH::Length(
            combined.maximum - output.center);
        if (!std::isfinite(output.radius)) {
            return {};
        }
        return output;
    }

} // namespace HIKARI::EDITOR
