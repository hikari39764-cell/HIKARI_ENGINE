#include "Scene/Sand/HIKARI_SandSurfaceSystem.h"

#include <algorithm>
#include <cmath>

#include "Core/HIKARI_FrameContext.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sand/HIKARI_SandSurfaceComponent.h"
#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"

namespace HIKARI {

    namespace {
        MATH::Vec3 TransformPoint(
            const MATH::Mat4& matrix,
            const MATH::Vec3& point) {

            const MATH::Vec4 transformed = matrix.TransformPoint({
                point.x, point.y, point.z, 1.0f });
            return {
                transformed.x,
                transformed.y,
                transformed.z
            };
        }

        bool IsDynamicSphere(const GameObject& object) {
            const ColliderComponent* collider =
                object.GetComponent<ColliderComponent>();
            if (collider == nullptr ||
                !collider->IsEnabled() ||
                collider->ResolveShape().type !=
                    PHYSICS::PhysicsShapeType::Sphere) {
                return false;
            }
            const PhysicsBodyComponent* body =
                object.GetComponent<PhysicsBodyComponent>();
            return body != nullptr &&
                body->IsEnabled() &&
                body->GetMotionType() ==
                    PHYSICS::PhysicsMotionType::Dynamic;
        }
    }

    void SandSurfaceSystem::OnWorldAttached(World& world) {
        queryService_ =
            world.Services().Find<SAND::SandSurfaceQueryService>();
        if (queryService_ != nullptr) {
            queryService_->Clear();
            states_.clear();
            Refresh(world);
        }
    }

    void SandSurfaceSystem::OnWorldDetached(World&) {
        if (queryService_ != nullptr) {
            queryService_->Clear();
        }
        queryService_ = nullptr;
        states_.clear();
    }

    void SandSurfaceSystem::PreFixedUpdate(
        World& world,
        const FrameContext& frame) {

        Refresh(world);
        ApplySphereContactStamps(world, frame.fixedDt);
    }

    void SandSurfaceSystem::PreRender(
        World& world,
        const FrameContext&) {

        Refresh(world);
    }

    void SandSurfaceSystem::Refresh(World& world) {
        if (queryService_ == nullptr) {
            return;
        }

        std::vector<SAND::SandSurfaceSnapshotEntry> surfaces{};
        surfaces.reserve(world.GetObjects().size());
        world.ForEachObjectWith<
            SandSurfaceComponent,
            ProceduralMeshComponent>(
            [&](GameObject& object,
                SandSurfaceComponent& sand,
                ProceduralMeshComponent& procedural) {

                const ProceduralMeshSettings mesh =
                    SanitizeProceduralMeshSettings(
                        procedural.GetSettings());
                if (mesh.kind != ProceduralMeshKind::Plane &&
                    mesh.kind != ProceduralMeshKind::GridPlane) {
                    return;
                }

                const SAND::SandSurfaceSettings settings =
                    SAND::SanitizeSandSurfaceSettings(
                        sand.GetSettings());
                const uint64_t key = object.GetRenderStableId();
                auto found = states_.find(key);
                SAND::SandSurfaceRuntimeStatePtr state =
                    found != states_.end()
                        ? found->second
                        : std::make_shared<SAND::SandSurfaceRuntimeState>();
                if (found == states_.end()) {
                    states_.emplace(key, state);
                }

                state->settings = settings;
                state->world = object.GetTransform().GetWorldMatrix();
                state->inverseWorld = MATH::Inverse(state->world);
                state->halfExtents = {
                    mesh.width * 0.5f,
                    mesh.depth * 0.5f
                };
                if (state->gridResolution != settings.gridResolution) {
                    state->gridResolution = settings.gridResolution;
                    const uint32_t stride =
                        settings.gridResolution + 1u;
                    state->heights.assign(
                        static_cast<size_t>(stride) * stride,
                        0.0f);
                }

                SAND::SandSurfaceSnapshotEntry entry{};
                entry.object = object.GetRuntimeHandle();
                entry.state = state;
                surfaces.push_back(std::move(entry));
            });
        queryService_->PublishSnapshot(std::move(surfaces));
    }

    void SandSurfaceSystem::ApplySphereContactStamps(
        World& world,
        float fixedDt) {

        if (queryService_ == nullptr || states_.empty()) {
            return;
        }
        const float safeDt = (std::max)(fixedDt, 1.0e-4f);

        world.ForEachObjectWith<ColliderComponent>(
            [&](GameObject& object, ColliderComponent&) {
                if (!IsDynamicSphere(object)) {
                    return;
                }
                const ColliderComponent* collider =
                    object.GetComponent<ColliderComponent>();
                const ResolvedColliderShape shape =
                    collider->ResolveShape();
                const MATH::Vec3 worldCenter =
                    object.GetTransform().position;
                const float worldRadius = shape.radius *
                    (std::max)(
                        object.GetTransform().scale.x,
                        (std::max)(
                            object.GetTransform().scale.y,
                            object.GetTransform().scale.z));

                for (const auto& [key, state] : states_) {
                    (void)key;
                    const MATH::Vec3 local =
                        TransformPoint(
                            state->inverseWorld,
                            worldCenter);
                    if (std::abs(local.x) >
                            state->halfExtents.x + worldRadius ||
                        std::abs(local.z) >
                            state->halfExtents.y + worldRadius) {
                        continue;
                    }

                    float surfaceHeight = 0.0f;
                    MATH::Vec3 surfaceNormal{};
                    if (!queryService_->SampleLocalHeight(
                            *state,
                            { local.x, local.z },
                            surfaceHeight,
                            surfaceNormal)) {
                        continue;
                    }
                    const float ballBottom =
                        local.y - worldRadius;
                    const float penetration =
                        surfaceHeight - ballBottom;
                    if (penetration <= 0.0f) {
                        continue;
                    }

                    const float depth = (std::min)(
                        state->settings.maxDepth * 0.55f,
                        penetration + 0.015f);
                    ApplyStamp(
                        *state,
                        local.x,
                        local.z,
                        (std::max)(
                            state->settings.stampRadius,
                            worldRadius * 0.8f),
                        depth);
                }
            });

        for (const auto& [key, state] : states_) {
            (void)key;
            RelaxState(*state, safeDt);
        }
    }

    void SandSurfaceSystem::ApplyStamp(
        SAND::SandSurfaceRuntimeState& state,
        float localX,
        float localZ,
        float radius,
        float depth) {

        const uint32_t resolution = state.gridResolution;
        const uint32_t stride = resolution + 1u;
        if (state.heights.size() !=
                static_cast<size_t>(stride) * stride ||
            depth <= 0.0f) {
            return;
        }

        const float cellX =
            (2.0f * state.halfExtents.x) /
            static_cast<float>(resolution);
        const float cellZ =
            (2.0f * state.halfExtents.y) /
            static_cast<float>(resolution);
        const float cellRadius =
            radius / (std::max)((std::min)(cellX, cellZ), 1.0e-4f);
        const int extent = static_cast<int>(cellRadius) + 2;

        const float safeDepth = (std::min)(depth, state.settings.maxDepth);
        const float rimStrength = state.settings.rimStrength;
        const float minimum = -state.settings.maxDepth;
        const float maximum = state.settings.maxDepth * rimStrength;

        const int centerX = static_cast<int>(std::lround(
            (localX / (2.0f * state.halfExtents.x) + 0.5f) *
            static_cast<float>(resolution)));
        const int centerZ = static_cast<int>(std::lround(
            (localZ / (2.0f * state.halfExtents.y) + 0.5f) *
            static_cast<float>(resolution)));

        for (int z = centerZ - extent;
            z <= centerZ + extent;
            ++z) {
            for (int x = centerX - extent;
                x <= centerX + extent;
                ++x) {
                if (x < 0 || z < 0 ||
                    x > static_cast<int>(resolution) ||
                    z > static_cast<int>(resolution)) {
                    continue;
                }
                const float dx =
                    (static_cast<float>(x) /
                        static_cast<float>(resolution) - 0.5f) *
                    2.0f * state.halfExtents.x - localX;
                const float dz =
                    (static_cast<float>(z) /
                        static_cast<float>(resolution) - 0.5f) *
                    2.0f * state.halfExtents.y - localZ;
                const float distance = std::sqrt(dx * dx + dz * dz);
                const float normalized =
                    std::clamp(distance / radius, 0.0f, 1.5f);

                // 中心へ滑らかに沈み、縁には砂を外へ押し出した rim を作る。
                // 硬い穴ではなく「砂が動いた」痕になるようにする。
                const float falloff = 1.0f -
                    3.0f * normalized * normalized +
                    2.0f * normalized * normalized * normalized;
                const float pit =
                    safeDepth * (std::max)(falloff, 0.0f);
                const float rim = rimStrength * safeDepth *
                    (1.0f - std::exp(-((normalized - 0.72f) *
                        (normalized - 0.72f) * 22.0f))) *
                    (1.0f - normalized);

                float& height =
                    state.heights[static_cast<size_t>(z) * stride + x];
                height = std::clamp(
                    height - pit + (std::max)(rim, 0.0f),
                    minimum,
                    maximum);
            }
        }
    }

    void SandSurfaceSystem::RelaxState(
        SAND::SandSurfaceRuntimeState& state,
        float fixedDt) {

        const uint32_t resolution = state.gridResolution;
        const uint32_t stride = resolution + 1u;
        if (state.heights.size() !=
            static_cast<size_t>(stride) * stride) {
            return;
        }

        const float decay = std::clamp(
            state.settings.refillPerSecond * fixedDt,
            0.0f,
            1.0f);
        if (decay > 0.0f) {
            for (float& height : state.heights) {
                height *= 1.0f - decay;
            }
        }

        const float diffusion = std::clamp(
            state.settings.diffusionPerSecond * fixedDt,
            0.0f,
            0.5f);
        if (diffusion <= 1.0e-4f) {
            return;
        }

        std::vector<float> source = state.heights;
        const float keep = 1.0f - diffusion;
        const float neighbor = diffusion * 0.25f;
        for (uint32_t z = 1u; z < resolution; ++z) {
            for (uint32_t x = 1u; x < resolution; ++x) {
                const size_t index =
                    static_cast<size_t>(z) * stride + x;
                state.heights[index] =
                    source[index] * keep +
                    (source[index - stride] +
                        source[index + stride] +
                        source[index - 1u] +
                        source[index + 1u]) * neighbor;
            }
        }
    }

} // namespace HIKARI
