#include "Scene/Sand/HIKARI_SandSurfaceSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>

#include "Core/HIKARI_FrameContext.h"
#include "Core/HIKARI_Logger.h"

#include <cstdio>
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sand/HIKARI_SandSurfaceFollowerComponent.h"
#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"
#include "Scene/Terrain/HIKARI_SurfacePreset.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceConversion.h"

namespace HIKARI {

    namespace {
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
        presentationTransforms_ =
            world.Services().Find<PresentationTransformService>();
        waterQueryService_ =
            world.Services().Find<WATER::WaterSurfaceQueryService>();
        if (waterQueryService_ != nullptr) {
            waterQueryService_->SetBathymetryProvider(this);
        }
        if (queryService_ != nullptr) {
            queryService_->Clear();
            objects_.clear();
            Refresh(world);
        }
    }

    void SandSurfaceSystem::OnWorldDetached(World&) {
        // 地形健全性セルフチェック。エディタでは常時、ゲームでは
        // HKR_TERRAIN_SELFTEST=1 で有効。ランタイム chunk 状態の整合と
        // 非ゼロ高さ・変形の有無を数値で報告し、編集/物理/描画の断点を
        // 一度に切り分ける。
#if defined(HIKARI_WITH_EDITOR)
        constexpr bool kRunSelfTest = true;
#else
        const bool kRunSelfTest =
            std::getenv("HKR_TERRAIN_SELFTEST") != nullptr;
#endif
        if (kRunSelfTest) {
            int badLayout = 0;
            int totalChunks = 0;
            int nonzeroHeight = 0;
            int nonzeroDeform = 0;
            for (const auto& [objKey, bucket] : objects_) {
                (void)objKey;
                for (const auto& [stateKey, state] : bucket.chunks) {
                    (void)stateKey;
                    ++totalChunks;
                    const std::size_t n =
                        static_cast<std::size_t>(state->cells + 1u) *
                        (state->cells + 1u);
                    if (state->heights.size() != n ||
                        state->baseHeights.size() != n ||
                        state->deformHeights.size() != n) {
                        ++badLayout;
                    }
                    for (float v : state->heights) {
                        if (v != 0.0f) { ++nonzeroHeight; break; }
                    }
                    for (float v : state->deformHeights) {
                        if (v != 0.0f) { ++nonzeroDeform; break; }
                    }
                }
            }
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "[TerrainSelfTest] chunks=%d badLayout=%d nonzeroHeight=%d nonzeroDeform=%d",
                totalChunks, badLayout, nonzeroHeight, nonzeroDeform);
            HIKARI_LOG_INFO(buf);
        }
        if (waterQueryService_ != nullptr) {
            waterQueryService_->SetBathymetryProvider(nullptr);
        }
        waterQueryService_ = nullptr;
        if (queryService_ != nullptr) {
            queryService_->Clear();
        }
        queryService_ = nullptr;
        objects_.clear();
    }

    bool SandSurfaceSystem::RaycastBathymetry(
        const WATER::WaterBathymetryRay& ray,
        float& outDistance) const noexcept {
        if (queryService_ == nullptr) {
            return false;
        }
        float sandHeight = 0.0f;
        MATH::Vec3 sandNormal{};
        if (!queryService_->SampleWorldHeight(
                ray.originWS, sandHeight, sandNormal)) {
            return false;
        }
        const float verticalDelta = ray.originWS.y - sandHeight;
        if (verticalDelta < 0.0f) {
            outDistance = 0.0f;
            return false;
        }
        const float downward = -(ray.directionWS.y);
        const float distance = downward > 1.0e-4f
            ? verticalDelta / downward
            : verticalDelta;
        if (distance < 0.0f || distance > ray.maximumDistance) {
            return false;
        }
        outDistance = distance;
        return true;
    }

    void SandSurfaceSystem::PreFixedUpdate(
        World& world,
        const FrameContext& frame) {
        Refresh(world);
        ApplyImprintSources(world, frame.fixedDt);

#if defined(HIKARI_WITH_EDITOR)
        // エディタ診断: 1 秒に 1 回、スタンプ源の有無と chunk 数を出す。
        // 「球を置いたのに凹まない」が設定起因かコード起因かを即座に
        // 切り分けるためのもの。
        {
            static int sDiagTick = 0;
            if (++sDiagTick >= 60) {
                sDiagTick = 0;
                int colliders = 0;
                int dynamicSpheres = 0;
                world.ForEachObjectWith<ColliderComponent>(
                    [&](GameObject& object, ColliderComponent&) {
                        ++colliders;
                        if (IsDynamicSphere(object)) {
                            ++dynamicSpheres;
                        }
                    });
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                    "[TerrainDiag] terrainObjs=%zu colliders=%d dynamicSpheres=%d",
                    objects_.size(), colliders, dynamicSpheres);
                HIKARI_LOG_INFO(buf);

                // 実動検証: 共有辺の高さ差（継ぎ目）を計測して出す。
                // 0 なら chunk 縫合と 1:1 変位パイプラインが成立している。
                float seamMaxDiff = 0.0f;
                int chunkTotal = 0;
                for (const auto& [objKey, bucket] : objects_) {
                    (void)objKey;
                    // ワールド頂点格子 -> 高さ（共有辺の一致検証用）。
                    std::map<uint64_t, float> worldVertexH{};
                    for (const auto& [stateKey, state] : bucket.chunks) {
                        (void)stateKey;
                        if (state == nullptr) { continue; }
                        ++chunkTotal;
                        const uint32_t stride = state->cells + 1u;
                        const int32_t ckx = static_cast<int32_t>(
                            std::lround(state->chunkOriginWS.x /
                                (std::max)(state->chunkWorldSize, 1.0e-4f)));
                        const int32_t ckz = static_cast<int32_t>(
                            std::lround(state->chunkOriginWS.y /
                                (std::max)(state->chunkWorldSize, 1.0e-4f)));
                        const std::size_t need =
                            static_cast<std::size_t>(stride) * stride;
                        if (state->heights.size() != need) { continue; }
                        for (uint32_t z = 0u; z <= state->cells; ++z) {
                            for (uint32_t x = 0u; x <= state->cells; ++x) {
                                const uint32_t gvx = static_cast<uint32_t>(
                                    static_cast<int64_t>(ckx) *
                                        state->cells + x);
                                const uint32_t gvz = static_cast<uint32_t>(
                                    static_cast<int64_t>(ckz) *
                                        state->cells + z);
                                const uint64_t wkey =
                                    (static_cast<uint64_t>(gvx) << 32u) | gvz;
                                const float h = state->heights[
                                    static_cast<std::size_t>(z) * stride + x];
                                const auto it = worldVertexH.find(wkey);
                                if (it == worldVertexH.end()) {
                                    worldVertexH.emplace(wkey, h);
                                } else {
                                    const float d = std::fabs(it->second - h);
                                    if (d > seamMaxDiff) { seamMaxDiff = d; }
                                }
                            }
                        }
                    }
                }
                std::snprintf(buf, sizeof(buf),
                    "[TerrainGeom] chunks=%d seamMaxDiff=%.6f",
                    chunkTotal, static_cast<double>(seamMaxDiff));
                HIKARI_LOG_INFO(buf);
            }
        }
#endif
    }

    void SandSurfaceSystem::PreRender(
        World& world,
        const FrameContext&) {
        Refresh(world);
    }

    void SandSurfaceSystem::SyncChunkFromAuthored(
        SAND::SandChunkRuntimeState& chunk,
        const TERRAIN::TerrainChunk& authored) {
        // 筆刷が彫った基底形状を baseHeights へ同期する。
        chunk.baseHeights = authored.heights;
        RebuildCombinedHeights(chunk);
    }

    void SandSurfaceSystem::RebuildCombinedHeights(
        SAND::SandChunkRuntimeState& chunk) {
        const std::size_t count = chunk.baseHeights.size();
        if (chunk.deformHeights.size() != count) {
            chunk.deformHeights.assign(count, 0.0f);
        }
        if (chunk.heights.size() != count) {
            chunk.heights.resize(count);
        }
        for (std::size_t i = 0u; i < count; ++i) {
            chunk.heights[i] =
                chunk.baseHeights[i] + chunk.deformHeights[i];
        }
    }

    void SandSurfaceSystem::Refresh(World& world) {
        if (queryService_ == nullptr) {
            return;
        }

        std::vector<SAND::SandSurfaceSnapshotEntry> entries{};

        world.ForEachObjectWith<TerrainSurfaceComponent>(
            [&](GameObject& object, TerrainSurfaceComponent& terrain) {
                const uint64_t objectKey = object.GetRenderStableId();
                ObjectChunks& bucket = objects_[objectKey];
                const TERRAIN::TerrainSurface& surface =
                    terrain.GetSurface();
                const float chunkSize = terrain.GetChunkWorldSize();
                const float cellSize = (std::max)(
                    surface.chunkCellSize, 0.01f);

                const uint64_t revision = terrain.GetRevision();
                const bool revisionChanged =
                    bucket.lastRevision != revision;
                bucket.lastRevision = revision;

                for (const auto& [key, authored] : surface.chunks) {
                    const uint64_t stateKey = ChunkStateKey(key);
                    SAND::SandChunkRuntimeStatePtr state;
                    auto found = bucket.chunks.find(stateKey);
                    if (found == bucket.chunks.end()) {
                        state = std::make_shared<
                            SAND::SandChunkRuntimeState>();
                        bucket.chunks.emplace(stateKey, state);
                        state->key = key;
                        state->cells = authored.cells;
                        state->cellSize = cellSize;
                        state->chunkWorldSize = chunkSize;
                        state->chunkOriginWS = {
                            static_cast<float>(key.x) * chunkSize,
                            static_cast<float>(key.z) * chunkSize
                        };
                        state->heightScale = surface.heightScale;
                        state->settings =
                            SAND::SanitizeSandSurfaceSettings(
                                TERRAIN::ToSandSurfaceSettings(surface));
                        SyncChunkFromAuthored(*state, authored);
                    } else {
                        state = found->second;
                        if (revisionChanged) {
                            // 筆刷編集が入ったので基底を再同期する。
                            SyncChunkFromAuthored(*state, authored);
                        }
                    }

                    SAND::SandSurfaceSnapshotEntry entry{};
                    entry.object = object.GetRuntimeHandle();
                    entry.chunk = state;
                    entries.push_back(std::move(entry));
                }

                // 消えた chunk を状態表から落とす。
                for (auto it = bucket.chunks.begin();
                    it != bucket.chunks.end();) {
                    const TERRAIN::TerrainChunkKey k{
                        static_cast<int32_t>(it->first >> 32u),
                        static_cast<int32_t>(it->first & 0xFFFFFFFFu)
                    };
                    if (surface.chunks.find(k) == surface.chunks.end()) {
                        it = bucket.chunks.erase(it);
                    } else {
                        ++it;
                    }
                }
            });

        queryService_->PublishSnapshot(std::move(entries));
    }

    void SandSurfaceSystem::GatherImprintSources(
        World& world,
        std::vector<ImprintRequest>& outRequests) {
        // 球: Collider(sphere) + dynamic body を持つオブジェクト。
        // 物理駆動の実位置は PresentationTransformService から取る。
        // 将来ここに Footprint / Wheel / Box などの列挙を追加する。
        world.ForEachObjectWith<ColliderComponent>(
            [&](GameObject& object, ColliderComponent&) {
                if (!IsDynamicSphere(object)) {
                    return;
                }
                const ColliderComponent* collider =
                    object.GetComponent<ColliderComponent>();
                const ResolvedColliderShape shape =
                    collider->ResolveShape();

                MATH::Vec3 worldCenter =
                    object.GetTransform().position;
                if (presentationTransforms_ != nullptr) {
                    MATH::Mat4 simWorld{};
                    if (presentationTransforms_->TryGetWorldMatrix(
                            object.GetRuntimeHandle(), simWorld)) {
                        MATH::Vec3 t{}, s{};
                        MATH::Quat r{};
                        if (MATH::DecomposeTRS(simWorld, t, r, s)) {
                            worldCenter = t;
                        }
                    }
                }
                const float worldRadius = shape.radius *
                    (std::max)(
                        object.GetTransform().scale.x,
                        (std::max)(
                            object.GetTransform().scale.y,
                            object.GetTransform().scale.z));

                // 移動を細分化してトンネリングを防ぐ。
                const uint64_t objectKey = object.GetRenderStableId();
                MATH::Vec3 previous = worldCenter;
                const auto previousIt =
                    previousSphereCenters_.find(objectKey);
                if (previousIt != previousSphereCenters_.end()) {
                    previous = previousIt->second;
                }
                previousSphereCenters_[objectKey] = worldCenter;

                const MATH::Vec3 delta = worldCenter - previous;
                const float travel = std::sqrt(
                    delta.x * delta.x +
                    delta.y * delta.y +
                    delta.z * delta.z);
                const float stepLength =
                    (std::max)(worldRadius * 0.5f, 0.05f);
                const int steps = (std::max)(
                    1, static_cast<int>(std::ceil(travel / stepLength)));
                for (int step = 0; step <= steps; ++step) {
                    const float t = steps > 0
                        ? static_cast<float>(step) /
                            static_cast<float>(steps)
                        : 0.0f;
                    ImprintRequest req{};
                    req.centerWS = {
                        previous.x + delta.x * t,
                        previous.y + delta.y * t,
                        previous.z + delta.z * t
                    };
                    req.radius = worldRadius;
                    outRequests.push_back(req);
                }
            });
        // フォロワー通道: SandSurfaceFollowerComponent（見た目を沙面に
        // 貼り付ける球など）も変形スタンプ源にする。dynamic body が無くても
        // これで「沙面に置いた物が沈む」が成立する。
        world.ForEachObjectWith<SandSurfaceFollowerComponent>(
            [&](GameObject& object, SandSurfaceFollowerComponent& follower) {
                const float radius = (std::max)(follower.ResolveRadius(), 0.05f);
                // 見た目の実位置は PresentationTransformService 優先。
                MATH::Vec3 center = object.GetTransform().position;
                if (presentationTransforms_ != nullptr) {
                    MATH::Mat4 simWorld{};
                    if (presentationTransforms_->TryGetWorldMatrix(
                            object.GetRuntimeHandle(), simWorld)) {
                        MATH::Vec3 t{}, s{};
                        MATH::Quat r{};
                        if (MATH::DecomposeTRS(simWorld, t, r, s)) {
                            center = t;
                        }
                    }
                }
                // 接触深さは「球の底が沙面より下に出た分」に任せるため、
                // ここでは中心をそのまま渡し、ApplyImprintSources 側の
                // softContact 計算に委ねる。
                ImprintRequest req{};
                req.centerWS = center;
                req.radius = radius;
                req.strength = 1.0f;
                outRequests.push_back(req);
            });
    }

    void SandSurfaceSystem::ApplyImprintSources(
        World& world,
        float fixedDt) {
        if (queryService_ == nullptr || objects_.empty()) {
            return;
        }
        const float safeDt = (std::max)(fixedDt, 1.0e-4f);

        std::vector<ImprintRequest> requests{};
        GatherImprintSources(world, requests);

        for (const ImprintRequest& req : requests) {
            const float worldRadius = (std::max)(req.radius, 0.01f);
            for (auto& [objKey, bucket] : objects_) {
                (void)objKey;
                for (auto& [stateKey, state] : bucket.chunks) {
                    (void)stateKey;
                    const float size = state->chunkWorldSize;
                    if (size <= 1.0e-4f) {
                        continue;
                    }
                    const float localX =
                        req.centerWS.x - state->chunkOriginWS.x;
                    const float localZ =
                        req.centerWS.z - state->chunkOriginWS.y;
                    if (localX < -worldRadius ||
                        localX > size + worldRadius ||
                        localZ < -worldRadius ||
                        localZ > size + worldRadius) {
                        continue;
                    }
                    const std::optional<TERRAIN::SurfacePreset>
                        preset = TERRAIN::ResolveSurfacePreset(
                            state->settings.presetId);
                    if (!preset.has_value() ||
                        preset->deform.behavior ==
                            TERRAIN::DeformBehaviorId::None) {
                        continue;
                    }

                    float surfaceHeight = 0.0f;
                    if (!queryService_->SampleChunkLocalHeight(
                            *state, localX, localZ,
                            surfaceHeight)) {
                        continue;
                    }
                    const float ballBottom =
                        req.centerWS.y - worldRadius;
                    const float sink = surfaceHeight - ballBottom;
                    const float softContact = worldRadius * 0.06f;
                    if (sink < -softContact) {
                        continue;
                    }
                    const float depth = std::clamp(
                        sink + softContact,
                        0.0f,
                        state->settings.maxDepth) * req.strength;
                    ApplySphereImprint(
                        *state,
                        req.centerWS.x,
                        req.centerWS.z,
                        worldRadius,
                        depth);
                }
            }
        }

        for (auto& [objKey, bucket] : objects_) {
            (void)objKey;
            for (auto& [stateKey, state] : bucket.chunks) {
                (void)stateKey;
                RelaxChunk(*state, safeDt);
            }
        }
    }

    void SandSurfaceSystem::ApplySphereContactStamps(
        World& world,
        float fixedDt) {
        // 旧エントリの互換維持。新規は ApplyImprintSources 経由。
        ApplyImprintSources(world, fixedDt);
    }


    void SandSurfaceSystem::ApplySphereImprint(
        SAND::SandChunkRuntimeState& chunk,
        float worldX,
        float worldZ,
        float worldRadius,
        float sinkDepth) {
        const uint32_t cells = (std::max)(chunk.cells, 1u);
        const uint32_t stride = cells + 1u;
        if (chunk.deformHeights.size() !=
                static_cast<std::size_t>(stride) * stride ||
            sinkDepth <= 0.0f) {
            return;
        }

        const float radius = (std::max)(worldRadius, 0.01f);
        const float depth = (std::min)(
            sinkDepth, chunk.settings.maxDepth);
        if (depth <= 0.0f) {
            return;
        }

        const float capRadius = std::sqrt((std::max)(
            2.0f * radius * depth - depth * depth, 0.0f));
        const float cell = (std::max)(chunk.cellSize, 1.0e-4f);
        const float rimBand = (std::max)(0.12f * radius, cell);
        const float reach = capRadius + rimBand;
        const float minimum = -chunk.settings.maxDepth;
        const float maximum =
            chunk.settings.maxDepth * chunk.settings.rimStrength;

        for (uint32_t z = 0u; z <= cells; ++z) {
            for (uint32_t x = 0u; x <= cells; ++x) {
                const float vx = chunk.chunkOriginWS.x +
                    static_cast<float>(x) * cell;
                const float vz = chunk.chunkOriginWS.y +
                    static_cast<float>(z) * cell;
                const float dx = vx - worldX;
                const float dz = vz - worldZ;
                const float r = std::sqrt(dx * dx + dz * dz);
                if (r > reach) {
                    continue;
                }

                float imprint = 0.0f;
                if (r <= capRadius) {
                    imprint = (std::max)(
                        0.0f,
                        std::sqrt((std::max)(
                            radius * radius - r * r, 0.0f)) -
                            (radius - depth));
                } else if (rimBand > 0.0f) {
                    const float t = (r - capRadius) / rimBand;
                    imprint = -chunk.settings.rimStrength * depth *
                        0.5f * (1.0f - t);
                }

                float& height =
                    chunk.deformHeights[
                        static_cast<std::size_t>(z) * stride + x];
                height = std::clamp(
                    height - imprint, minimum, maximum);
            }
        }
    }

    void SandSurfaceSystem::RelaxChunk(
        SAND::SandChunkRuntimeState& chunk,
        float fixedDt) {
        const uint32_t cells = (std::max)(chunk.cells, 1u);
        const uint32_t stride = cells + 1u;
        if (chunk.deformHeights.size() !=
            static_cast<std::size_t>(stride) * stride) {
            return;
        }

        const float decay = std::clamp(
            chunk.settings.refillPerSecond * fixedDt, 0.0f, 1.0f);
        if (decay > 0.0f) {
            for (float& height : chunk.deformHeights) {
                height *= 1.0f - decay;
            }
        }

        const float diffusion = std::clamp(
            chunk.settings.diffusionPerSecond * fixedDt, 0.0f, 0.5f);
        if (diffusion > 1.0e-4f) {
            std::vector<float> source = chunk.deformHeights;
            const float keep = 1.0f - diffusion;
            const float neighbor = diffusion * 0.25f;
            for (uint32_t z = 1u; z < cells; ++z) {
                for (uint32_t x = 1u; x < cells; ++x) {
                    const std::size_t index =
                        static_cast<std::size_t>(z) * stride + x;
                    chunk.deformHeights[index] =
                        source[index] * keep +
                        (source[index - stride] +
                            source[index + stride] +
                            source[index - 1u] +
                            source[index + 1u]) * neighbor;
                }
            }
        }
        RebuildCombinedHeights(chunk);
    }

} // namespace HIKARI
