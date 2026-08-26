#include "Scene/Sand/HIKARI_SandSurfaceSystem.h"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

#include "Core/HIKARI_FrameContext.h"
#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"
#include "Scene/Terrain/HIKARI_SurfacePreset.h"
#include "Scene/Terrain/HIKARI_TerrainInteraction.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceResponse.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceConversion.h"

namespace HIKARI {

    namespace {
        constexpr float kFootprintBasisEpsilon = 1.0e-5f;
        constexpr float kRecoverySnapEpsilon = 1.0e-4f;
        constexpr float kDeformableMaskClearThreshold = 1.0e-3f;

        uint64_t HashAuthoredHeights(
            const TERRAIN::TerrainChunk& chunk) noexcept {
            uint64_t hash = 1469598103934665603ull;
            const auto mix = [&](uint32_t value) {
                hash ^= static_cast<uint64_t>(value);
                hash *= 1099511628211ull;
            };
            mix(chunk.cells);
            for (float value : chunk.heights) {
                mix(std::bit_cast<uint32_t>(value));
            }
            // 層ペイントも authored terrain state なので同じ signature に含め、
            // paint revision で runtime response mask を再投影する。
            mix(chunk.layerCount);
            for (float value : chunk.layerWeights) {
                mix(std::bit_cast<uint32_t>(value));
            }
            return hash;
        }

        uint64_t HashAuthoredLayerPolicy(
            const TERRAIN::TerrainSurface& surface,
            uint32_t layerCount) noexcept {
            uint64_t hash = 1469598103934665603ull;
            const auto mix = [&](uint32_t value) {
                hash ^= static_cast<uint64_t>(value);
                hash *= 1099511628211ull;
            };
            mix(layerCount);
            mix(static_cast<uint32_t>(surface.layerPresetIds.size()));
            for (const std::string& presetId : surface.layerPresetIds) {
                for (const unsigned char character : presetId) {
                    mix(static_cast<uint32_t>(character));
                }
                mix(0u);
            }
            for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                mix(SAND::IsSandLayerDeformable(surface, layer) ? 1u : 0u);
            }
            return hash;
        }

        uint64_t HashAuthoredCoverage(
            const TERRAIN::TerrainChunk& chunk) noexcept {
            uint64_t hash = 1469598103934665603ull;
            const auto mix = [&](uint32_t value) {
                hash ^= static_cast<uint64_t>(value);
                hash *= 1099511628211ull;
            };
            mix(chunk.cells);
            for (float value : chunk.coverage) {
                mix(std::bit_cast<uint32_t>(value));
            }
            return hash;
        }

        bool FootprintBasisUsable(
            const MATH::Vec2& tangent,
            const MATH::Vec2& bitangent) noexcept {
            const float tangentLengthSq =
                tangent.x * tangent.x + tangent.y * tangent.y;
            const float bitangentLengthSq =
                bitangent.x * bitangent.x + bitangent.y * bitangent.y;
            const float determinant =
                tangent.x * bitangent.y - tangent.y * bitangent.x;
            const float basisScale = std::sqrt(
                tangentLengthSq * bitangentLengthSq);
            return std::isfinite(basisScale) &&
                std::isfinite(determinant) &&
                basisScale > 1.0e-8f &&
                std::fabs(determinant) >
                    kFootprintBasisEpsilon * basisScale;
        }

        bool EvaluateFootprintFactor(
            TERRAIN::TerrainFootprintKind kind,
            float dx,
            float dz,
            const MATH::Vec2& tangent,
            const MATH::Vec2& bitangent,
            float radiusX,
            float radiusZ,
            float& outFactor) noexcept {
            outFactor = 0.0f;
            if (!FootprintBasisUsable(tangent, bitangent)) {
                return false;
            }
            const float determinant =
                tangent.x * bitangent.y - tangent.y * bitangent.x;
            // local XZ の差分を、world footprint の tangent/bitangent 単位へ
            // 戻す。非一様 scale 後の二軸は直交しないため dot では解けない。
            const float along =
                (dx * bitangent.y - dz * bitangent.x) / determinant;
            const float across =
                (tangent.x * dz - tangent.y * dx) / determinant;
            const float safeZ = (std::max)(radiusZ, 0.01f);
            const float safeX = kind ==
                    TERRAIN::TerrainFootprintKind::Capsule
                ? (std::max)(radiusX, safeZ)
                : (std::max)(radiusX, 0.01f);
            if (kind == TERRAIN::TerrainFootprintKind::Capsule) {
                const float capRadius = safeZ;
                const float segmentHalf = (std::max)(
                    safeX - capRadius, 0.0f);
                const float axial = (std::max)(
                    std::fabs(along) - segmentHalf, 0.0f);
                outFactor = std::sqrt(
                    axial * axial + across * across) / capRadius;
                return std::isfinite(outFactor);
            }
            outFactor = std::sqrt(
                along * along / (safeX * safeX) +
                across * across / (safeZ * safeZ));
            return std::isfinite(outFactor);
        }
    }

    void SandSurfaceSystem::OnWorldAttached(World& world) {
        queryService_ =
            world.Services().Find<SAND::SandSurfaceQueryService>();
        waterQueryService_ =
            world.Services().Find<WATER::WaterSurfaceQueryService>();
        runtimePlayState_ =
            world.Services().Find<RuntimePlayStateService>();
        if (waterQueryService_ != nullptr) {
            waterQueryService_->SetBathymetryProvider(this);
        }
        if (queryService_ != nullptr) {
            queryService_->Clear();
            objects_.clear();
            hasPublishedSnapshot_ = false;
            Refresh(world);
        }
    }

    void SandSurfaceSystem::OnWorldDetached(World&) {
        if (waterQueryService_ != nullptr) {
            waterQueryService_->SetBathymetryProvider(nullptr);
        }
        waterQueryService_ = nullptr;
        runtimePlayState_ = nullptr;
        if (queryService_ != nullptr) {
            queryService_->Clear();
        }
        queryService_ = nullptr;
        objects_.clear();
        hasPublishedSnapshot_ = false;
    }

    bool SandSurfaceSystem::RaycastBathymetry(
        const WATER::WaterBathymetryRay& ray,
        float& outDistance) const noexcept {
        if (queryService_ == nullptr) {
            return false;
        }
        if (MATH::Dot(ray.directionWS, ray.directionWS) <= 1.0e-12f ||
            ray.maximumDistance <= 0.0f) {
            return false;
        }
        const MATH::Vec3 direction =
            MATH::Normalize(ray.directionWS);
        constexpr uint32_t steps = 128u;
        float previousT = 0.0f;
        float previousAbove = 0.0f;
        bool hasPrevious = false;
        for (uint32_t step = 0u; step <= steps; ++step) {
            const float t = ray.maximumDistance *
                static_cast<float>(step) / static_cast<float>(steps);
            const MATH::Vec3 point = ray.originWS + direction * t;
            float surfaceHeight = 0.0f;
            MATH::Vec3 surfaceNormal{};
            if (!queryService_->SampleWorldHeight(
                    point, surfaceHeight, surfaceNormal)) {
                hasPrevious = false;
                continue;
            }
            const float above = point.y - surfaceHeight;
            if (hasPrevious && previousAbove > 0.0f && above <= 0.0f) {
                float lo = previousT;
                float hi = t;
                for (uint32_t iteration = 0u;
                    iteration < 16u;
                    ++iteration) {
                    const float mid = (lo + hi) * 0.5f;
                    const MATH::Vec3 midPoint =
                        ray.originWS + direction * mid;
                    float midHeight = 0.0f;
                    if (!queryService_->SampleWorldHeight(
                            midPoint, midHeight, surfaceNormal) ||
                        midPoint.y > midHeight) {
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }
                outDistance = (lo + hi) * 0.5f;
                return true;
            }
            previousT = t;
            previousAbove = above;
            hasPrevious = true;
        }
        return false;
    }

    void SandSurfaceSystem::PreFixedUpdate(
        World& world,
        const FrameContext&) {
        Refresh(world);
    }

    void SandSurfaceSystem::PostFixedUpdate(
        World& world,
        const FrameContext& frame) {
        if (ApplyInteractions(
                world.FixedEvents().Read<
                    TERRAIN::TerrainInteractionSample>(),
                frame.fixedDt)) {
            // SandSurfaceSystem は physics/contact 後の order 140 で動く一方、
            // RenderSubmissionSystem は PreRender order 100 で snapshot を読む。
            // ここで発行しないと renderer は PreFixed の古い高さを bake して
            // dirty を消費し、その後の PreRender Refresh は 1 frame 遅すぎる。
            Refresh(world);
        }
    }

    void SandSurfaceSystem::Update(
        World& world,
        const FrameContext& frame) {
        if (runtimePlayState_ == nullptr ||
            runtimePlayState_->IsActive()) {
            return;
        }
        Refresh(world);
        if (ApplyInteractions(
                world.FrameEvents().Read<
                    TERRAIN::TerrainInteractionSample>(),
                std::clamp(frame.unscaledDt, 0.0f, 0.1f))) {
            Refresh(world);
        }
    }

    void SandSurfaceSystem::PreRender(
        World& world,
        const FrameContext&) {
        Refresh(world);
    }

    void SandSurfaceSystem::SyncChunkFromAuthored(
        SAND::SandChunkRuntimeState& chunk,
        const TERRAIN::TerrainChunk& authored,
        const TERRAIN::TerrainSurface& surface) {
        // authored 格子を runtime response 格子へ双線形投影する。
        // runtime deformation は別配列なので、筆刷更新で痕跡を混ぜない。
        const uint32_t sourceCells = (std::max)(authored.cells, 1u);
        const uint32_t sourceStride = sourceCells + 1u;
        const uint32_t targetCells = (std::max)(chunk.cells, 1u);
        const uint32_t targetStride = targetCells + 1u;
        const std::size_t sourceCount =
            static_cast<std::size_t>(sourceStride) * sourceStride;
        const std::size_t targetCount =
            static_cast<std::size_t>(targetStride) * targetStride;
        chunk.baseHeights.assign(targetCount, 0.0f);
        if (authored.heights.size() == sourceCount) {
            const auto source = [&](uint32_t x, uint32_t z) {
                return authored.heights[
                    static_cast<std::size_t>(z) * sourceStride + x];
            };
            for (uint32_t z = 0u; z <= targetCells; ++z) {
                const float fz = static_cast<float>(z) *
                    static_cast<float>(sourceCells) /
                    static_cast<float>(targetCells);
                const uint32_t z0 = static_cast<uint32_t>(fz);
                const uint32_t z1 = (std::min)(z0 + 1u, sourceCells);
                const float tz = fz - static_cast<float>(z0);
                for (uint32_t x = 0u; x <= targetCells; ++x) {
                    const float fx = static_cast<float>(x) *
                        static_cast<float>(sourceCells) /
                        static_cast<float>(targetCells);
                    const uint32_t x0 = static_cast<uint32_t>(fx);
                    const uint32_t x1 = (std::min)(x0 + 1u, sourceCells);
                    const float tx = fx - static_cast<float>(x0);
                    chunk.baseHeights[
                        static_cast<std::size_t>(z) * targetStride + x] =
                        std::lerp(
                            std::lerp(source(x0, z0), source(x1, z0), tx),
                            std::lerp(source(x0, z1), source(x1, z1), tx),
                            tz);
                }
            }
        }
        SAND::ProjectSandChunkDeformableMask(
            surface,
            authored,
            targetCells,
            chunk.deformableMask);
        // 完全な hard layer に隠れた response を残すと、後から sand を塗った
        // ときに復活する。混合境界の feather は保ち、ほぼ 0 の内部だけ消す。
        const std::size_t responseCount = chunk.deformableMask.size();
        if (chunk.elasticHeights.size() == responseCount &&
            chunk.plasticHeights.size() == responseCount &&
            chunk.recoveryHold.size() == responseCount) {
            for (std::size_t index = 0u; index < responseCount; ++index) {
                if (chunk.deformableMask[index] <=
                    kDeformableMaskClearThreshold) {
                    chunk.elasticHeights[index] = 0.0f;
                    chunk.plasticHeights[index] = 0.0f;
                    chunk.recoveryHold[index] = 0.0f;
                }
            }
            chunk.hasDeformation = std::any_of(
                chunk.elasticHeights.begin(),
                chunk.elasticHeights.end(),
                [](float value) {
                    return std::fabs(value) > kRecoverySnapEpsilon;
                }) ||
                std::any_of(
                    chunk.plasticHeights.begin(),
                    chunk.plasticHeights.end(),
                    [](float value) {
                        return std::fabs(value) > kRecoverySnapEpsilon;
                    });
            if (!chunk.hasDeformation) {
                chunk.needsRelaxation = false;
            }
        }
        RebuildCombinedHeights(chunk);
        ++chunk.stateRevision;
    }

    void SandSurfaceSystem::RebuildCombinedHeights(
        SAND::SandChunkRuntimeState& chunk) {
        const std::size_t count = chunk.baseHeights.size();
        if (chunk.elasticHeights.size() != count) {
            chunk.elasticHeights.assign(count, 0.0f);
        }
        if (chunk.plasticHeights.size() != count) {
            chunk.plasticHeights.assign(count, 0.0f);
        }
        if (chunk.recoveryHold.size() != count) {
            chunk.recoveryHold.assign(count, 0.0f);
        }
        if (chunk.elasticScratch.size() != count) {
            chunk.elasticScratch.assign(count, 0.0f);
        }
        if (chunk.deformHeights.size() != count) {
            chunk.deformHeights.assign(count, 0.0f);
        }
        if (chunk.heights.size() != count) {
            chunk.heights.resize(count);
        }
        if (chunk.deformableMask.size() != count) {
            // 旧 runtime state には layer mask が無い。Refresh が authored
            // layer weights を投影するまでは、従来どおり全体を変形可能とする。
            chunk.deformableMask.assign(count, 1.0f);
        }
        const float minimum = -(std::max)(
            chunk.settings.maxDepth, 0.0f);
        const float maximum = (std::max)(
            chunk.settings.maxDepth, 0.0f) * (std::max)(
                chunk.settings.rimStrength, 0.0f);
        bool hasDeformation = false;
        for (std::size_t i = 0u; i < count; ++i) {
            const TERRAIN::TerrainSurfaceDisplacementChannels channels =
                TERRAIN::ClampTerrainSurfaceDisplacementChannels(
                    {
                        chunk.elasticHeights[i],
                        chunk.plasticHeights[i],
                    },
                    minimum,
                    maximum);
            chunk.elasticHeights[i] = channels.elastic;
            chunk.plasticHeights[i] = channels.plastic;
            const float rawDeformation = channels.Combined();
            hasDeformation = hasDeformation ||
                std::fabs(rawDeformation) > kRecoverySnapEpsilon;
            chunk.deformHeights[i] =
                SAND::EvaluateSandChunkFinalHeight(
                    0.0f,
                    rawDeformation,
                    chunk.deformableMask[i],
                    chunk.settings.deformationEnabled);
            chunk.heights[i] = SAND::EvaluateSandChunkFinalHeight(
                chunk.baseHeights[i],
                rawDeformation,
                chunk.deformableMask[i],
                chunk.settings.deformationEnabled);
        }
        chunk.hasDeformation = hasDeformation;
        if (!hasDeformation) {
            chunk.needsRelaxation = false;
        }
    }

    void SandSurfaceSystem::Refresh(World& world) {
        if (queryService_ == nullptr) {
            return;
        }

        std::vector<SAND::SandSurfaceSnapshotEntry> entries{};
        std::unordered_set<uint64_t> activeTerrainObjects{};
        bool publishNeeded = !hasPublishedSnapshot_;
        TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
        const uint64_t layerRegistryRevision =
            TERRAIN::SurfaceLayerRegistry::Get().GetRevision();

        world.ForEachObjectWith<TerrainSurfaceComponent>(
            [&](GameObject& object, TerrainSurfaceComponent& terrain) {
                const uint64_t objectKey = object.GetRenderStableId();
                activeTerrainObjects.insert(objectKey);
                auto [bucketIt, insertedBucket] =
                    objects_.try_emplace(objectKey);
                ObjectChunks& bucket = bucketIt->second;
                const TERRAIN::TerrainSurface& surface =
                    terrain.GetSurface();
                const float chunkSize = terrain.GetChunkWorldSize();
                const SAND::SandSurfaceSettings nextSettings =
                    SAND::SanitizeSandSurfaceSettings(
                        TERRAIN::ToSandSurfaceSettings(surface));

                const uint64_t revision = terrain.GetRevision();
                const bool revisionChanged =
                    insertedBucket || bucket.lastRevision != revision;
                bucket.lastRevision = revision;
                const uint64_t coverageRevision =
                    terrain.GetCoverageRevision();
                const bool coverageRevisionChanged =
                    insertedBucket ||
                    bucket.lastCoverageRevision != coverageRevision;
                bucket.lastCoverageRevision = coverageRevision;
                const bool layerRegistryChanged = insertedBucket ||
                    bucket.lastLayerRegistryRevision !=
                        layerRegistryRevision;
                bucket.lastLayerRegistryRevision = layerRegistryRevision;
                const bool bucketSettingsChanged = !bucket.hasSettings ||
                    !SAND::SandSurfaceSettingsEqual(
                        bucket.lastSettings, nextSettings);
                bucket.lastSettings = nextSettings;
                bucket.hasSettings = true;
                bucket.chunkWorldSize = chunkSize;
                bucket.maximumStampRadius = (std::max)(
                    nextSettings.stampRadius, 0.0f);
                // 高さ場はローカル空間。スタンプ源をローカルへ写す逆行列を
                // キャッシュする（オブジェクトが動いたら毎フレーム新鮮に）。
                bucket.localToWorld =
                    object.GetTransform().GetWorldMatrix();
                if (!bucket.hasPublishedTransform ||
                    std::memcmp(
                        &bucket.publishedLocalToWorld,
                        &bucket.localToWorld,
                        sizeof(MATH::Mat4)) != 0) {
                    bucket.publishedLocalToWorld = bucket.localToWorld;
                    bucket.hasPublishedTransform = true;
                    publishNeeded = true;
                }
                bucket.worldToLocal = MATH::Inverse(bucket.localToWorld);
                bucket.owner = &object;
                MATH::Vec3 translation{};
                MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
                MATH::Quat rotation{};
                if (MATH::DecomposeTRS(
                        bucket.localToWorld,
                        translation,
                        rotation,
                        scale)) {
                    bucket.scaleX =
                        (std::max)(std::fabs(scale.x), 1.0e-4f);
                    bucket.scaleY =
                        (std::max)(std::fabs(scale.y), 1.0e-4f);
                    bucket.scaleZ =
                        (std::max)(std::fabs(scale.z), 1.0e-4f);
                } else {
                    bucket.scaleX = 1.0f;
                    bucket.scaleY = 1.0f;
                    bucket.scaleZ = 1.0f;
                }

                const bool authoredRefreshNeeded = insertedBucket ||
                    revisionChanged || coverageRevisionChanged ||
                    layerRegistryChanged || bucketSettingsChanged ||
                    bucket.chunks.size() != surface.chunks.size() ||
                    bucket.authoredCoverageSignatures.size() !=
                        surface.chunks.size();
                if (!authoredRefreshNeeded) {
                    for (const uint64_t stateKey :
                            bucket.dirtySnapshotKeys) {
                        const auto state = bucket.chunks.find(stateKey);
                        if (state == bucket.chunks.end() ||
                            state->second == nullptr) {
                            continue;
                        }
                        SAND::SandChunkRuntimeSnapshotPtr immutable =
                            SAND::MakeSandChunkRuntimeSnapshot(
                                *state->second);
                        bucket.snapshots.insert_or_assign(
                            stateKey, std::move(immutable));
                        bucket.snapshotRevisions.insert_or_assign(
                            stateKey, state->second->stateRevision);
                        publishNeeded = true;
                    }
                    bucket.dirtySnapshotKeys.clear();
                    return;
                }

                bool coverageTopologyChanged =
                    bucket.authoredCoverageSignatures.size() !=
                        surface.chunks.size();
                bool coverageLayoutChanged = false;
                for (const auto& [key, authored] : surface.chunks) {
                    const uint64_t stateKey = ChunkStateKey(key);
                    if (!bucket.authoredCoverageSignatures.contains(
                            stateKey)) {
                        coverageTopologyChanged = true;
                    }
                    const uint32_t runtimeCells = std::clamp(
                        authored.cells *
                            nextSettings.responseResolutionScale,
                        authored.cells,
                        authored.cells * 4u);
                    const float runtimeCellSize = chunkSize /
                        static_cast<float>((std::max)(runtimeCells, 1u));
                    const auto state = bucket.chunks.find(stateKey);
                    coverageLayoutChanged = coverageLayoutChanged ||
                        (state != bucket.chunks.end() &&
                            (state->second->authoredCells != authored.cells ||
                             state->second->cells != runtimeCells ||
                             std::fabs(
                                 state->second->cellSize - runtimeCellSize) >
                                    1.0e-6f ||
                             std::fabs(
                                 state->second->chunkWorldSize - chunkSize) >
                                    1.0e-6f));
                }

                std::unordered_set<uint64_t> changedCoverageSources{};
                if (coverageRevisionChanged || coverageTopologyChanged) {
                    for (const auto& [key, authored] : surface.chunks) {
                        const uint64_t stateKey = ChunkStateKey(key);
                        const uint64_t signature =
                            HashAuthoredCoverage(authored);
                        const auto previous =
                            bucket.authoredCoverageSignatures.find(stateKey);
                        if (previous ==
                                bucket.authoredCoverageSignatures.end() ||
                            previous->second != signature) {
                            changedCoverageSources.insert(stateKey);
                        }
                        bucket.authoredCoverageSignatures.insert_or_assign(
                            stateKey, signature);
                    }
                }
                for (auto it = bucket.authoredCoverageSignatures.begin();
                    it != bucket.authoredCoverageSignatures.end();) {
                    const TERRAIN::TerrainChunkKey key{
                        static_cast<int32_t>(it->first >> 32u),
                        static_cast<int32_t>(it->first & 0xFFFFFFFFu) };
                    if (!surface.chunks.contains(key)) {
                        coverageTopologyChanged = true;
                        it = bucket.authoredCoverageSignatures.erase(it);
                    } else {
                        ++it;
                    }
                }

                const bool edgeFalloffChanged =
                    !bucket.hasCoverageProjectionSettings ||
                    bucket.lastCoverageEdgeFalloff !=
                        nextSettings.edgeFalloff;
                bucket.lastCoverageEdgeFalloff = nextSettings.edgeFalloff;
                bucket.hasCoverageProjectionSettings = true;
                const bool fullCoverageResample =
                    coverageTopologyChanged || coverageLayoutChanged ||
                    edgeFalloffChanged;
                std::unordered_set<uint64_t> coverageResampleKeys{};
                if (fullCoverageResample) {
                    coverageResampleKeys.reserve(surface.chunks.size());
                    for (const auto& [key, authored] : surface.chunks) {
                        (void)authored;
                        coverageResampleKeys.insert(ChunkStateKey(key));
                    }
                } else if (!changedCoverageSources.empty()) {
                    const int64_t neighborRing = (std::max)(
                        int64_t{ 1 },
                        static_cast<int64_t>(std::ceil(
                            (std::max)(nextSettings.edgeFalloff, 0.0f) /
                            (std::max)(chunkSize, 0.01f))));
                    // 通常の dab は raw coverage が変わった chunk と、falloff が
                    // 到達する隣接 ring だけを高分 response 格子へ再投影する。
                    for (const auto& [candidateKey, authored] :
                            surface.chunks) {
                        (void)authored;
                        for (const uint64_t sourceKey :
                                changedCoverageSources) {
                            const int32_t sourceX = static_cast<int32_t>(
                                sourceKey >> 32u);
                            const int32_t sourceZ = static_cast<int32_t>(
                                sourceKey & 0xFFFFFFFFu);
                            const int64_t dx =
                                static_cast<int64_t>(candidateKey.x) -
                                sourceX;
                            const int64_t dz =
                                static_cast<int64_t>(candidateKey.z) -
                                sourceZ;
                            if (dx >= -neighborRing && dx <= neighborRing &&
                                dz >= -neighborRing && dz <= neighborRing) {
                                coverageResampleKeys.insert(
                                    ChunkStateKey(candidateKey));
                                break;
                            }
                        }
                    }
                }

                for (const auto& [key, authored] : surface.chunks) {
                    const uint64_t stateKey = ChunkStateKey(key);
                    const uint32_t runtimeCells = std::clamp(
                        authored.cells *
                            nextSettings.responseResolutionScale,
                        authored.cells,
                        authored.cells * 4u);
                    const float runtimeCellSize = chunkSize /
                        static_cast<float>((std::max)(runtimeCells, 1u));
                    SAND::SandChunkRuntimeStatePtr state;
                    auto found = bucket.chunks.find(stateKey);
                    const bool created = found == bucket.chunks.end();
                    bool layoutChanged = false;
                    if (created) {
                        state = std::make_shared<
                            SAND::SandChunkRuntimeState>();
                        bucket.chunks.emplace(stateKey, state);
                    } else {
                        state = found->second;
                        layoutChanged =
                            state->authoredCells != authored.cells ||
                            state->cells != runtimeCells ||
                            std::fabs(state->cellSize - runtimeCellSize) >
                                1.0e-6f ||
                            std::fabs(state->chunkWorldSize - chunkSize) >
                                1.0e-6f;
                    }
                    const auto previousSignature =
                        bucket.authoredHeightSignatures.find(stateKey);
                    const auto previousLayerPolicySignature =
                        bucket.authoredLayerPolicySignatures.find(stateKey);
                    uint64_t authoredSignature =
                        previousSignature !=
                            bucket.authoredHeightSignatures.end()
                        ? previousSignature->second
                        : 0u;
                    const uint64_t layerPolicySignature =
                        HashAuthoredLayerPolicy(
                            surface,
                            authored.layerCount);
                    bool authoredChanged = created || layoutChanged ||
                        previousSignature ==
                            bucket.authoredHeightSignatures.end();
                    if (authoredChanged || revisionChanged) {
                        const uint64_t nextSignature =
                            HashAuthoredHeights(authored);
                        authoredChanged = authoredChanged ||
                            nextSignature != authoredSignature;
                        authoredSignature = nextSignature;
                    }
                    const bool layerPolicyChanged = created || layoutChanged ||
                        previousLayerPolicySignature ==
                            bucket.authoredLayerPolicySignatures.end() ||
                        previousLayerPolicySignature->second !=
                            layerPolicySignature;
                    // authoredSignature は height+layerWeights 側で、policy 側は
                    // 独立比較する。これにより TerrainSurface revision が同じでも
                    // registry の変更を検出できる。
                    authoredChanged = authoredChanged || layerPolicyChanged;
                    const bool stateSettingsChanged = created ||
                        !SAND::SandSurfaceSettingsEqual(
                            state->settings, nextSettings);
                    bucket.authoredHeightSignatures[stateKey] =
                        authoredSignature;
                    bucket.authoredLayerPolicySignatures[stateKey] =
                        layerPolicySignature;
                    state->key = key;
                    state->authoredCells = authored.cells;
                    state->cells = runtimeCells;
                    state->cellSize = runtimeCellSize;
                    state->chunkWorldSize = chunkSize;
                    state->chunkOriginLocal = {
                        static_cast<float>(key.x) * chunkSize,
                        static_cast<float>(key.z) * chunkSize
                    };
                    state->heightScale = surface.heightScale;
                    state->settings = nextSettings;
                    if (authoredChanged) {
                        // 筆刷内容だけでなく grid/chunk 尺度の変更も同じ経路で
                        // 再同期し、古い stride で query / imprint しない。
                        SyncChunkFromAuthored(*state, authored, surface);
                    } else if (stateSettingsChanged) {
                        ++state->stateRevision;
                    }
                    if (created || layoutChanged ||
                        coverageResampleKeys.contains(stateKey)) {
                        const uint32_t stride = state->cells + 1u;
                        std::vector<float> nextCoverage(
                            static_cast<std::size_t>(stride) * stride);
                        for (uint32_t z = 0u; z <= state->cells; ++z) {
                            for (uint32_t x = 0u; x <= state->cells; ++x) {
                                const float localX =
                                    state->chunkOriginLocal.x +
                                    static_cast<float>(x) * state->cellSize;
                                const float localZ =
                                    state->chunkOriginLocal.y +
                                    static_cast<float>(z) * state->cellSize;
                                nextCoverage[
                                    static_cast<std::size_t>(z) * stride + x] =
                                    terrain.SampleLocalCoverage({
                                        localX, 0.0f, localZ });
                            }
                        }
                        if (state->coverage != nextCoverage) {
                            state->coverage = std::move(nextCoverage);
                            ++state->stateRevision;
                        }
                    }
                    if (state->needsRelaxation) {
                        bucket.activeDeformationKeys.insert(stateKey);
                    } else {
                        bucket.activeDeformationKeys.erase(stateKey);
                    }
                    const auto snapshotRevision =
                        bucket.snapshotRevisions.find(stateKey);
                    auto snapshot = bucket.snapshots.find(stateKey);
                    if (snapshot == bucket.snapshots.end() ||
                        snapshotRevision == bucket.snapshotRevisions.end() ||
                        snapshotRevision->second != state->stateRevision) {
                        SAND::SandChunkRuntimeSnapshotPtr immutable =
                            SAND::MakeSandChunkRuntimeSnapshot(*state);
                        bucket.snapshots[stateKey] = immutable;
                        bucket.snapshotRevisions[stateKey] =
                            state->stateRevision;
                        publishNeeded = true;
                    }
                }
                bucket.dirtySnapshotKeys.clear();

                // 消えた chunk を状態表から落とす。
                for (auto it = bucket.chunks.begin();
                    it != bucket.chunks.end();) {
                    const TERRAIN::TerrainChunkKey k{
                        static_cast<int32_t>(it->first >> 32u),
                        static_cast<int32_t>(it->first & 0xFFFFFFFFu)
                    };
                    if (surface.chunks.find(k) == surface.chunks.end()) {
                        bucket.snapshots.erase(it->first);
                        bucket.snapshotRevisions.erase(it->first);
                        bucket.authoredHeightSignatures.erase(it->first);
                        bucket.authoredLayerPolicySignatures.erase(it->first);
                        bucket.authoredCoverageSignatures.erase(it->first);
                        bucket.activeDeformationKeys.erase(it->first);
                        bucket.dirtySnapshotKeys.erase(it->first);
                        publishNeeded = true;
                        it = bucket.chunks.erase(it);
                    } else {
                        ++it;
                    }
                }
            });

        for (auto it = objects_.begin(); it != objects_.end();) {
            if (!activeTerrainObjects.contains(it->first)) {
                publishNeeded = true;
                it = objects_.erase(it);
            } else {
                ++it;
            }
        }

        if (publishNeeded) {
            std::size_t entryCount = 0u;
            for (const auto& [objectKey, bucket] : objects_) {
                (void)objectKey;
                entryCount += bucket.snapshots.size();
            }
            entries.reserve(entryCount);
            std::vector<uint64_t> orderedObjectKeys{};
            orderedObjectKeys.reserve(objects_.size());
            for (const auto& [objectKey, bucket] : objects_) {
                (void)bucket;
                orderedObjectKeys.push_back(objectKey);
            }
            std::sort(
                orderedObjectKeys.begin(), orderedObjectKeys.end());
            std::vector<uint64_t> orderedSnapshotKeys{};
            for (const uint64_t objectKey : orderedObjectKeys) {
                const auto bucketIt = objects_.find(objectKey);
                if (bucketIt == objects_.end()) {
                    continue;
                }
                const ObjectChunks& bucket = bucketIt->second;
                if (bucket.owner == nullptr) {
                    continue;
                }
                orderedSnapshotKeys.clear();
                orderedSnapshotKeys.reserve(bucket.snapshots.size());
                for (const auto& [stateKey, snapshot] : bucket.snapshots) {
                    if (snapshot != nullptr) {
                        orderedSnapshotKeys.push_back(stateKey);
                    }
                }
                std::sort(
                    orderedSnapshotKeys.begin(),
                    orderedSnapshotKeys.end());
                const MATH::Mat4 normalMatrix =
                    MATH::BuildNormalMatrixFromWorld(
                        bucket.localToWorld);
                for (const uint64_t stateKey : orderedSnapshotKeys) {
                    const auto snapshot = bucket.snapshots.find(stateKey);
                    if (snapshot == bucket.snapshots.end() ||
                        snapshot->second == nullptr) {
                        continue;
                    }
                    SAND::SandSurfaceSnapshotEntry entry{};
                    entry.object = bucket.owner->GetRuntimeHandle();
                    entry.chunk = snapshot->second;
                    entry.localToWorld = bucket.localToWorld;
                    entry.worldToLocal = bucket.worldToLocal;
                    entry.normalMatrix = normalMatrix;
                    entries.push_back(std::move(entry));
                }
            }
            queryService_->PublishSnapshot(std::move(entries));
            hasPublishedSnapshot_ = true;
        }
    }

    bool SandSurfaceSystem::ApplyInteractions(
        std::span<const TERRAIN::TerrainInteractionSample> requests,
        float deltaSeconds) {
        if (queryService_ == nullptr || objects_.empty()) {
            return false;
        }
        const float safeDt = (std::max)(deltaSeconds, 0.0f);
        std::unordered_set<uint64_t> changedTerrainObjects{};

        for (const TERRAIN::TerrainInteractionSample& rawRequest : requests) {
            if (!TERRAIN::IsFiniteTerrainInteractionSample(rawRequest)) {
                continue;
            }
            const TERRAIN::TerrainInteractionSample req =
                TERRAIN::SanitizeTerrainInteractionSample(rawRequest);
            if (req.phase == TERRAIN::TerrainInteractionPhase::End) {
                continue;
            }
            // 自動 interaction は physics が確定した terrain だけへ送る。
            // invalid target を全 terrain へ撒く旧 fallback は、重なった地形や
            // 空中の source を誤って変形するため禁止する。
            if (!req.targetSurface.IsValid()) {
                continue;
            }
            float worldRadiusX = (std::max)(
                req.footprint.halfExtentsWS.x, 0.01f);
            const float worldRadiusZ = (std::max)(
                req.footprint.halfExtentsWS.y, 0.01f);
            if (req.footprint.kind ==
                    TERRAIN::TerrainFootprintKind::Capsule) {
                // Capsule の x は円帽を含む全半長。半径より短い入力は
                // 円として扱える最小半長へ正規化する。
                worldRadiusX = (std::max)(worldRadiusX, worldRadiusZ);
            }
            const float worldVerticalRadius = (std::max)(
                req.footprint.verticalRadiusWS, 0.01f);
            for (auto& [objKey, bucket] : objects_) {
                if (req.targetSurface.IsValid() &&
                    (bucket.owner == nullptr ||
                     bucket.owner->GetRuntimeHandle() != req.targetSurface)) {
                    continue;
                }
                // ワールド中心をこのオブジェクトのローカル空間へ変換。
                const MATH::Vec4 cLocal4 = bucket.worldToLocal.TransformPoint(
                    { req.positionWS.x, req.positionWS.y,
                      req.positionWS.z, 1.0f });
                const float invW = std::fabs(cLocal4.w) > 1.0e-6f
                    ? 1.0f / cLocal4.w
                    : 1.0f;
                const MATH::Vec3 cLocal{
                    cLocal4.x * invW,
                    cLocal4.y * invW,
                    cLocal4.z * invW };
                MATH::Vec2 localTangent{ 1.0f, 0.0f };
                MATH::Vec2 localBitangent{ 0.0f, 1.0f };
                // basis は footprint 座標 1 単位を terrain local XZ へ写す。
                // Sphere は local 単位を使い、定向 footprint は world 単位を使う。
                float footprintRadiusX = worldRadiusX /
                    (std::max)(bucket.scaleX, 1.0e-4f);
                float footprintRadiusZ = worldRadiusZ /
                    (std::max)(bucket.scaleZ, 1.0e-4f);
                if (req.footprint.kind !=
                        TERRAIN::TerrainFootprintKind::Sphere) {
                    const MATH::Vec4 tangent4 =
                        bucket.worldToLocal.TransformPoint({
                            req.footprint.tangentWS.x,
                            req.footprint.tangentWS.y,
                            req.footprint.tangentWS.z,
                            0.0f });
                    const MATH::Vec3 bitangentWS = MATH::Normalize(
                        MATH::Cross(
                            req.normalWS,
                            req.footprint.tangentWS));
                    const MATH::Vec4 bitangent4 =
                        bucket.worldToLocal.TransformPoint({
                            bitangentWS.x,
                            bitangentWS.y,
                            bitangentWS.z,
                            0.0f });
                    localTangent = { tangent4.x, tangent4.z };
                    localBitangent = { bitangent4.x, bitangent4.z };
                    footprintRadiusX = worldRadiusX;
                    footprintRadiusZ = worldRadiusZ;
                }
                if (!FootprintBasisUsable(
                        localTangent, localBitangent)) {
                    // terrain parameter 平面へ投影できない近特異姿勢は、
                    // 不定な巨大 imprint にせずこの surface では拒否する。
                    continue;
                }
                const float localVerticalRadius = worldVerticalRadius /
                    (std::max)(bucket.scaleY, 1.0e-4f);
                const float contactBoundsX =
                    std::fabs(localTangent.x) * footprintRadiusX +
                    std::fabs(localBitangent.x) * footprintRadiusZ;
                const float contactBoundsZ =
                    std::fabs(localTangent.y) * footprintRadiusX +
                    std::fabs(localBitangent.y) * footprintRadiusZ;

                // Collider footprint は contact gate のまま保持し、実際に描く
                // imprint だけを authored stampRadius で拡張する。物理接触を
                // 偽装せず、球・足・車輪へ同じ animation profile を再利用する。
                float responseRadiusX = footprintRadiusX;
                float responseRadiusZ = footprintRadiusZ;
                const float authoredStampRadius =
                    bucket.maximumStampRadius;
                if (req.footprint.kind ==
                        TERRAIN::TerrainFootprintKind::Sphere) {
                    responseRadiusX = (std::max)(
                        responseRadiusX, authoredStampRadius);
                    responseRadiusZ = (std::max)(
                        responseRadiusZ, authoredStampRadius);
                } else if (authoredStampRadius > 0.0f) {
                    const float tangentLength = std::sqrt(
                        localTangent.x * localTangent.x +
                        localTangent.y * localTangent.y);
                    const float bitangentLength = std::sqrt(
                        localBitangent.x * localBitangent.x +
                        localBitangent.y * localBitangent.y);
                    const float currentMinor = (std::max)(
                        (std::min)(
                            responseRadiusX * tangentLength,
                            responseRadiusZ * bitangentLength),
                        0.01f);
                    const float desiredMinor = authoredStampRadius *
                        (req.sourceKind ==
                            TERRAIN::TerrainInteractionSourceKind::
                                CharacterFoot
                            ? 0.55f
                            : 0.75f);
                    const float maximumScale = req.sourceKind ==
                            TERRAIN::TerrainInteractionSourceKind::
                                CharacterFoot
                        ? 1.75f
                        : 1.5f;
                    const float responseScale = std::clamp(
                        desiredMinor / currentMinor,
                        1.0f,
                        maximumScale);
                    responseRadiusX *= responseScale;
                    responseRadiusZ *= responseScale;
                }
                const float responseBoundsX =
                    std::fabs(localTangent.x) * responseRadiusX +
                    std::fabs(localBitangent.x) * responseRadiusZ;
                const float responseBoundsZ =
                    std::fabs(localTangent.y) * responseRadiusX +
                    std::fabs(localBitangent.y) * responseRadiusZ;

                std::vector<SAND::SandChunkRuntimeState*> candidates{};
                float minimumCellSize = FLT_MAX;
                float responseMaxDepth = 0.0f;
                const auto appendCandidate = [&] (
                    const SAND::SandChunkRuntimeStatePtr& state) {
                    if (state == nullptr) {
                        return;
                    }
                    const float size = state->chunkWorldSize;
                    if (size <= 1.0e-4f) {
                        return;
                    }
                    const float localX =
                        cLocal.x - state->chunkOriginLocal.x;
                    const float localZ =
                        cLocal.z - state->chunkOriginLocal.y;
                    if (localX < -responseBoundsX ||
                        localX > size + responseBoundsX ||
                        localZ < -responseBoundsZ ||
                        localZ > size + responseBoundsZ) {
                        return;
                    }
                    if (!state->settings.deformationEnabled) {
                        return;
                    }
                    candidates.push_back(state.get());
                    minimumCellSize = (std::min)(
                        minimumCellSize,
                        (std::max)(state->cellSize, 1.0e-4f));
                    responseMaxDepth = (std::max)(
                        responseMaxDepth,
                        (std::max)(state->settings.maxDepth, 0.0f));
                };

                // Uniform chunk grid なので footprint の local bounds から
                // key 範囲を直接求める。通常の足跡は 1～4 chunk lookup で
                // 完結し、terrain 全体の chunk 数へ比例しない。
                const float candidateChunkSize = bucket.chunkWorldSize;
                const auto toChunkCoordinate = [](
                    double localCoordinate,
                    float size,
                    int32_t& outCoordinate) noexcept {
                    if (!std::isfinite(localCoordinate) ||
                        !std::isfinite(size) || size <= 1.0e-4f) {
                        return false;
                    }
                    const double value = std::floor(
                        localCoordinate / static_cast<double>(size));
                    if (value < static_cast<double>(
                            (std::numeric_limits<int32_t>::min)()) ||
                        value > static_cast<double>(
                            (std::numeric_limits<int32_t>::max)())) {
                        return false;
                    }
                    outCoordinate = static_cast<int32_t>(value);
                    return true;
                };
                int32_t minimumChunkX = 0;
                int32_t minimumChunkZ = 0;
                int32_t maximumChunkX = 0;
                int32_t maximumChunkZ = 0;
                const bool keyRangeValid = toChunkCoordinate(
                        static_cast<double>(cLocal.x - responseBoundsX),
                        candidateChunkSize,
                        minimumChunkX) &&
                    toChunkCoordinate(
                        static_cast<double>(cLocal.z - responseBoundsZ),
                        candidateChunkSize,
                        minimumChunkZ) &&
                    toChunkCoordinate(
                        static_cast<double>(cLocal.x + responseBoundsX),
                        candidateChunkSize,
                        maximumChunkX) &&
                    toChunkCoordinate(
                        static_cast<double>(cLocal.z + responseBoundsZ),
                        candidateChunkSize,
                        maximumChunkZ);
                if (!keyRangeValid) {
                    continue;
                }
                const uint64_t rangeWidth = static_cast<uint64_t>(
                    static_cast<int64_t>(maximumChunkX) -
                    static_cast<int64_t>(minimumChunkX) + 1);
                const uint64_t rangeHeight = static_cast<uint64_t>(
                    static_cast<int64_t>(maximumChunkZ) -
                    static_cast<int64_t>(minimumChunkZ) + 1);
                const uint64_t directLookupBudget =
                    static_cast<uint64_t>(bucket.chunks.size()) * 4u + 16u;
                const bool useDirectLookup = rangeHeight == 0u ||
                    rangeWidth <= directLookupBudget / rangeHeight;
                if (useDirectLookup) {
                    candidates.reserve(static_cast<std::size_t>((std::min)(
                        rangeWidth * rangeHeight,
                        static_cast<uint64_t>(bucket.chunks.size()))));
                    for (int64_t z = minimumChunkZ;
                        z <= static_cast<int64_t>(maximumChunkZ);
                        ++z) {
                        for (int64_t x = minimumChunkX;
                            x <= static_cast<int64_t>(maximumChunkX);
                            ++x) {
                            const auto found = bucket.chunks.find(
                                ChunkStateKey({
                                    static_cast<int32_t>(x),
                                    static_cast<int32_t>(z),
                                }));
                            if (found != bucket.chunks.end()) {
                                appendCandidate(found->second);
                            }
                        }
                    }
                } else {
                    // 巨大な authored footprint は lookup 空間より実在 chunk
                    // を 1 回だけ走査する方が有界。通常キャラクターはこの
                    // fallback へ入らない。
                    for (const auto& [stateKey, state] : bucket.chunks) {
                        (void)stateKey;
                        appendCandidate(state);
                    }
                }
                if (candidates.empty()) {
                    continue;
                }

                // 球心だけで判定すると、球心が coverage の外でも球面が
                // 可視海岸へ触れているケースを落とす。足跡楕円全体をサンプルし、
                // GPU と同じ edge sink 済み地表との最大侵入量を求める。
                const auto sampleSurface =
                    [&](float objectLocalX,
                        float objectLocalZ,
                        float& height,
                        float& coverage) {
                        bool sampled = false;
                        float bestCoverage = -FLT_MAX;
                        for (const SAND::SandChunkRuntimeState* state :
                            candidates) {
                            const float x = objectLocalX -
                                state->chunkOriginLocal.x;
                            const float z = objectLocalZ -
                                state->chunkOriginLocal.y;
                            constexpr float epsilon = 1.0e-3f;
                            if (x < -epsilon ||
                                x > state->chunkWorldSize + epsilon ||
                                z < -epsilon ||
                                z > state->chunkWorldSize + epsilon) {
                                continue;
                            }
                            float candidateHeight = 0.0f;
                            float candidateCoverage = 0.0f;
                            if (!queryService_->SampleChunkLocalSurface(
                                    *state,
                                    x,
                                    z,
                                    candidateHeight,
                                    candidateCoverage)) {
                                continue;
                            }
                            if (!sampled ||
                                candidateCoverage > bestCoverage) {
                                sampled = true;
                                bestCoverage = candidateCoverage;
                                height = candidateHeight;
                                coverage = candidateCoverage;
                            }
                        }
                        return sampled;
                    };

                const bool sphereFootprint =
                    req.footprint.kind ==
                        TERRAIN::TerrainFootprintKind::Sphere;
                float bestPenetration = -FLT_MAX;
                const auto evaluateContact = [&](float x, float z) {
                    float radialFactor = 0.0f;
                    if (!EvaluateFootprintFactor(
                            req.footprint.kind,
                            x - cLocal.x,
                            z - cLocal.z,
                            localTangent,
                            localBitangent,
                            footprintRadiusX,
                            footprintRadiusZ,
                            radialFactor)) {
                        return;
                    }
                    if (radialFactor > 1.0f) {
                        return;
                    }
                    float surfaceHeight = 0.0f;
                    float coverage = 0.0f;
                    if (!sampleSurface(
                            x, z, surfaceHeight, coverage)) {
                        return;
                    }
                    if (coverage <
                        SAND::kSandSurfaceVisibleCoverageThreshold) {
                        return;
                    }
                    if (sphereFootprint) {
                        const float sphereSurfaceY = cLocal.y -
                            localVerticalRadius * std::sqrt(
                                (std::max)(
                                    1.0f - radialFactor * radialFactor,
                                    0.0f));
                        bestPenetration = (std::max)(
                            bestPenetration,
                            surfaceHeight - sphereSurfaceY);
                    } else {
                        bestPenetration = (std::max)(
                            bestPenetration, 0.0f);
                    }
                };

                evaluateContact(cLocal.x, cLocal.z);
                const uint32_t samplesX = std::clamp(
                    static_cast<uint32_t>(std::ceil(
                        2.0f * contactBoundsX / minimumCellSize)),
                    4u,
                    48u);
                const uint32_t samplesZ = std::clamp(
                    static_cast<uint32_t>(std::ceil(
                        2.0f * contactBoundsZ / minimumCellSize)),
                    4u,
                    48u);
                for (uint32_t z = 0u; z <= samplesZ; ++z) {
                    const float sampleZ = cLocal.z - contactBoundsZ +
                        2.0f * contactBoundsZ *
                        static_cast<float>(z) /
                        static_cast<float>(samplesZ);
                    for (uint32_t x = 0u; x <= samplesX; ++x) {
                        const float sampleX = cLocal.x - contactBoundsX +
                            2.0f * contactBoundsX *
                            static_cast<float>(x) /
                            static_cast<float>(samplesX);
                        evaluateContact(sampleX, sampleZ);
                    }
                }

                const bool holdOnly = req.strength <= 1.0e-5f;
                const bool settleWhileHeld =
                    holdOnly && req.allowSettling;
                const bool refreshHoldOnly =
                    holdOnly && !settleWhileHeld;
                if (sphereFootprint) {
                    const float softContact =
                        localVerticalRadius * 0.06f;
                    if (bestPenetration < -softContact) {
                        continue;
                    }
                }

                const SAND::SandSurfaceSettings& responseSettings =
                    candidates.front()->settings;
                TERRAIN::DeformParams profile{};
                profile.behavior = responseSettings.deformationEnabled
                    ? TERRAIN::DeformBehaviorId::SphereCap
                    : TERRAIN::DeformBehaviorId::None;
                profile.maxDepth = responseSettings.maxDepth;
                profile.restDepthScale = responseSettings.restDepthScale;
                profile.minimumDepth = responseSettings.minimumDepth;
                profile.settleStartRatio =
                    responseSettings.settleStartRatio;
                profile.settleSeconds = responseSettings.settleSeconds;
                profile.impactDepthScale =
                    responseSettings.impactDepthScale;
                const float approachSpeedWS = (std::max)(
                    -MATH::Dot(req.relativeVelocityWS, req.normalWS),
                    0.0f);
                const TERRAIN::TerrainSurfaceResponseResult response =
                    TERRAIN::EvaluateTerrainSurfaceResponse(
                        profile,
                        {
                            localVerticalRadius,
                            sphereFootprint
                                ? (std::max)(bestPenetration, 0.0f)
                                : req.penetrationDepthWS /
                                    (std::max)(bucket.scaleY, 1.0e-4f),
                            approachSpeedWS /
                                (std::max)(bucket.scaleY, 1.0e-4f),
                            req.contactAgeSeconds,
                            req.contactSeconds,
                            settleWhileHeld ? 1.0f : req.strength,
                        });
                const float depth = (std::min)(
                    response.targetDepthLocal,
                    responseMaxDepth);
                if (depth <= 0.0f && !refreshHoldOnly) {
                    continue;
                }
                for (SAND::SandChunkRuntimeState* state : candidates) {
                    if (ApplyStylizedImprint(
                            *state,
                            req.footprint.kind,
                            cLocal.x,
                            cLocal.z,
                            responseRadiusX,
                            responseRadiusZ,
                            localVerticalRadius,
                            localTangent,
                            localBitangent,
                            req.footprint.edgeSoftness,
                            depth,
                            refreshHoldOnly)) {
                        const uint64_t stateKey =
                            ChunkStateKey(state->key);
                        bucket.activeDeformationKeys.insert(stateKey);
                        bucket.dirtySnapshotKeys.insert(stateKey);
                        changedTerrainObjects.insert(objKey);
                    }
                }
            }
        }

        for (auto& [objKey, bucket] : objects_) {
            for (auto active = bucket.activeDeformationKeys.begin();
                active != bucket.activeDeformationKeys.end();) {
                const uint64_t stateKey = *active;
                const auto state = bucket.chunks.find(stateKey);
                if (state == bucket.chunks.end() ||
                    state->second == nullptr) {
                    active = bucket.activeDeformationKeys.erase(active);
                    continue;
                }
                if (RelaxChunk(*state->second, safeDt)) {
                    bucket.dirtySnapshotKeys.insert(stateKey);
                    changedTerrainObjects.insert(objKey);
                }
                if (!state->second->needsRelaxation) {
                    active = bucket.activeDeformationKeys.erase(active);
                } else {
                    ++active;
                }
            }
            if (StitchRuntimeBorders(bucket)) {
                changedTerrainObjects.insert(objKey);
            }
        }
        for (const uint64_t objectKey : changedTerrainObjects) {
            const auto found = objects_.find(objectKey);
            if (found != objects_.end() && found->second.owner != nullptr) {
                found->second.owner->MarkRenderStateDirty();
            }
        }
        return !changedTerrainObjects.empty();
    }

    bool SandSurfaceSystem::StitchRuntimeBorders(ObjectChunks& bucket) {
        if (bucket.activeDeformationKeys.empty()) {
            return false;
        }
        std::unordered_set<uint64_t> stitchKeySet{};
        stitchKeySet.reserve(
            bucket.activeDeformationKeys.size() * 9u);
        for (const uint64_t activeKey :
                bucket.activeDeformationKeys) {
            const int32_t activeX = static_cast<int32_t>(
                static_cast<uint32_t>(activeKey >> 32u));
            const int32_t activeZ = static_cast<int32_t>(
                static_cast<uint32_t>(activeKey & 0xFFFFFFFFu));
            for (int32_t dz = -1; dz <= 1; ++dz) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    const int64_t x = static_cast<int64_t>(activeX) + dx;
                    const int64_t z = static_cast<int64_t>(activeZ) + dz;
                    if (x < (std::numeric_limits<int32_t>::min)() ||
                        x > (std::numeric_limits<int32_t>::max)() ||
                        z < (std::numeric_limits<int32_t>::min)() ||
                        z > (std::numeric_limits<int32_t>::max)()) {
                        continue;
                    }
                    const uint64_t candidateKey = ChunkStateKey({
                        static_cast<int32_t>(x),
                        static_cast<int32_t>(z),
                    });
                    if (bucket.chunks.contains(candidateKey)) {
                        stitchKeySet.insert(candidateKey);
                    }
                }
            }
        }
        if (stitchKeySet.size() < 2u) {
            return false;
        }
        std::vector<uint64_t> stitchKeys(
            stitchKeySet.begin(), stitchKeySet.end());
        std::sort(stitchKeys.begin(), stitchKeys.end());
        struct BoundaryValue {
            TERRAIN::TerrainSurfaceDisplacementChannels channels{};
            float hold = 0.0f;
            uint32_t count = 0u;
            bool initialized = false;
        };
        std::unordered_map<uint64_t, BoundaryValue> shared{};
        shared.reserve(stitchKeys.size() * 256u);
        const auto globalKey = [](int32_t x, int32_t z) noexcept {
            return (static_cast<uint64_t>(
                        static_cast<uint32_t>(x)) << 32u) |
                static_cast<uint32_t>(z);
        };
        for (const uint64_t stateKey : stitchKeys) {
            const auto foundState = bucket.chunks.find(stateKey);
            if (foundState == bucket.chunks.end() ||
                foundState->second == nullptr) {
                continue;
            }
            const SAND::SandChunkRuntimeStatePtr& state =
                foundState->second;
            const uint32_t cells = state->cells;
            const uint32_t stride = cells + 1u;
            const std::size_t count =
                static_cast<std::size_t>(stride) * stride;
            if (cells == 0u || state->elasticHeights.size() != count ||
                state->plasticHeights.size() != count ||
                state->recoveryHold.size() != count) {
                continue;
            }
            for (uint32_t z = 0u; z <= cells; ++z) {
                for (uint32_t x = 0u; x <= cells; ++x) {
                    if (x != 0u && x != cells &&
                        z != 0u && z != cells) {
                        continue;
                    }
                    const int32_t gx = state->key.x *
                        static_cast<int32_t>(cells) +
                        static_cast<int32_t>(x);
                    const int32_t gz = state->key.z *
                        static_cast<int32_t>(cells) +
                        static_cast<int32_t>(z);
                    const std::size_t index =
                        static_cast<std::size_t>(z) * stride + x;
                    if (state->deformableMask.size() == count &&
                        (state->deformableMask[index] <=
                            kDeformableMaskClearThreshold ||
                         !std::isfinite(state->deformableMask[index]))) {
                        continue;
                    }
                    BoundaryValue& value = shared[globalKey(gx, gz)];
                    const float minimum = -(std::max)(
                        state->settings.maxDepth, 0.0f);
                    const float maximum = (std::max)(
                        state->settings.maxDepth, 0.0f) * (std::max)(
                            state->settings.rimStrength, 0.0f);
                    const TERRAIN::TerrainSurfaceDisplacementChannels
                        candidate =
                            TERRAIN::ClampTerrainSurfaceDisplacementChannels(
                                {
                                    state->elasticHeights[index],
                                    state->plasticHeights[index],
                                },
                                minimum,
                                maximum);
                    value.channels = value.initialized
                        ? TERRAIN::SelectTerrainSurfaceDisplacementChannels(
                            value.channels, candidate)
                        : candidate;
                    value.initialized = true;
                    value.hold = (std::max)(
                        value.hold, state->recoveryHold[index]);
                    ++value.count;
                }
            }
        }

        std::unordered_set<SAND::SandChunkRuntimeState*> touched{};
        for (const uint64_t stateKey : stitchKeys) {
            const auto foundState = bucket.chunks.find(stateKey);
            if (foundState == bucket.chunks.end() ||
                foundState->second == nullptr) {
                continue;
            }
            const SAND::SandChunkRuntimeStatePtr& state =
                foundState->second;
            const uint32_t cells = state->cells;
            const uint32_t stride = cells + 1u;
            const std::size_t count =
                static_cast<std::size_t>(stride) * stride;
            if (cells == 0u || state->elasticHeights.size() != count ||
                state->plasticHeights.size() != count ||
                state->recoveryHold.size() != count) {
                continue;
            }
            for (uint32_t z = 0u; z <= cells; ++z) {
                for (uint32_t x = 0u; x <= cells; ++x) {
                    if (x != 0u && x != cells &&
                        z != 0u && z != cells) {
                        continue;
                    }
                    const int32_t gx = state->key.x *
                        static_cast<int32_t>(cells) +
                        static_cast<int32_t>(x);
                    const int32_t gz = state->key.z *
                        static_cast<int32_t>(cells) +
                        static_cast<int32_t>(z);
                    const auto found = shared.find(globalKey(gx, gz));
                    if (found == shared.end() || found->second.count < 2u) {
                        continue;
                    }
                    const std::size_t index =
                        static_cast<std::size_t>(z) * stride + x;
                    if (state->deformableMask.size() == count &&
                        (state->deformableMask[index] <=
                            kDeformableMaskClearThreshold ||
                         !std::isfinite(state->deformableMask[index]))) {
                        if (std::fabs(state->elasticHeights[index]) >
                                kRecoverySnapEpsilon ||
                            std::fabs(state->plasticHeights[index]) >
                                kRecoverySnapEpsilon ||
                            state->recoveryHold[index] > 0.0f) {
                            state->elasticHeights[index] = 0.0f;
                            state->plasticHeights[index] = 0.0f;
                            state->recoveryHold[index] = 0.0f;
                            touched.insert(state.get());
                        }
                        continue;
                    }
                    const BoundaryValue& value = found->second;
                    const float minimum = -(std::max)(
                        state->settings.maxDepth, 0.0f);
                    const float maximum = (std::max)(
                        state->settings.maxDepth, 0.0f) * (std::max)(
                            state->settings.rimStrength, 0.0f);
                    TERRAIN::TerrainSurfaceDisplacementChannels channels =
                        TERRAIN::ClampTerrainSurfaceDisplacementChannels(
                            value.channels,
                            minimum,
                            maximum);
                    if (value.hold <= 0.0f) {
                        // contact が終わった seam は pair 全体を plastic へ
                        // 畳み、隣接 chunk 間で lifecycle を一致させる。
                        channels = { 0.0f, channels.Combined() };
                    }
                    const float nextHold = (std::max)(
                        state->recoveryHold[index], value.hold);
                    if (std::fabs(
                            state->elasticHeights[index] -
                                channels.elastic) >
                            1.0e-7f ||
                        std::fabs(
                            state->plasticHeights[index] -
                                channels.plastic) >
                            1.0e-7f ||
                        std::fabs(
                            state->recoveryHold[index] - nextHold) >
                            1.0e-7f) {
                        state->elasticHeights[index] = channels.elastic;
                        state->plasticHeights[index] = channels.plastic;
                        state->recoveryHold[index] = nextHold;
                        touched.insert(state.get());
                    }
                }
            }
        }

        for (SAND::SandChunkRuntimeState* state : touched) {
            state->responseGridActive = true;
            RebuildCombinedHeights(*state);
            state->needsRelaxation = state->hasDeformation &&
                std::any_of(
                    state->elasticHeights.begin(),
                    state->elasticHeights.end(),
                    [](float value) {
                        return std::fabs(value) > kRecoverySnapEpsilon;
                    });
            ++state->stateRevision;
            const uint64_t stateKey = ChunkStateKey(state->key);
            if (state->needsRelaxation) {
                bucket.activeDeformationKeys.insert(stateKey);
            } else {
                bucket.activeDeformationKeys.erase(stateKey);
            }
            bucket.dirtySnapshotKeys.insert(stateKey);
        }
        return !touched.empty();
    }


    bool SandSurfaceSystem::ApplyStylizedImprint(
        SAND::SandChunkRuntimeState& chunk,
        TERRAIN::TerrainFootprintKind footprintKind,
        float localX,
        float localZ,
        float localRadiusX,
        float localRadiusZ,
        float localVerticalRadius,
        const MATH::Vec2& localTangent,
        const MATH::Vec2& localBitangent,
        float edgeSoftness,
        float sinkDepth,
        bool holdOnly) {
        const uint32_t cells = (std::max)(chunk.cells, 1u);
        const uint32_t stride = cells + 1u;
        const std::size_t vertexCount =
            static_cast<std::size_t>(stride) * stride;
        if (chunk.deformHeights.size() != vertexCount ||
            chunk.elasticHeights.size() != vertexCount ||
            chunk.plasticHeights.size() != vertexCount ||
            chunk.recoveryHold.size() != vertexCount ||
            chunk.coverage.size() != vertexCount ||
            (!holdOnly && sinkDepth <= 0.0f)) {
            return false;
        }

        const float radiusZ = (std::max)(localRadiusZ, 0.01f);
        const float radiusX = footprintKind ==
                TERRAIN::TerrainFootprintKind::Capsule
            ? (std::max)(localRadiusX, radiusZ)
            : (std::max)(localRadiusX, 0.01f);
        if (!FootprintBasisUsable(localTangent, localBitangent)) {
            return false;
        }
        const float verticalRadius =
            (std::max)(localVerticalRadius, 0.01f);
        float depth = (std::min)(
            sinkDepth,
            chunk.settings.maxDepth);
        if (footprintKind == TERRAIN::TerrainFootprintKind::Sphere) {
            depth = (std::min)(depth, 2.0f * verticalRadius);
        }
        if (depth <= 0.0f && !holdOnly) {
            return false;
        }

        const float capFactor = holdOnly
            ? 1.0f
            : (footprintKind == TERRAIN::TerrainFootprintKind::Sphere
                ? std::sqrt((std::max)(
                    2.0f * verticalRadius * depth - depth * depth,
                    0.0f)) / verticalRadius
                : 1.0f);
        const float cell = (std::max)(chunk.cellSize, 1.0e-4f);
        const float localRadiusXLength = radiusX * std::sqrt(
            localTangent.x * localTangent.x +
            localTangent.y * localTangent.y);
        const float localRadiusZLength = radiusZ * std::sqrt(
            localBitangent.x * localBitangent.x +
            localBitangent.y * localBitangent.y);
        const float rimBandFactor = (std::max)({
            chunk.settings.rimWidthScale,
            cell / (std::max)(localRadiusXLength, 0.01f),
            cell / (std::max)(localRadiusZLength, 0.01f) });
        const float reachFactor = capFactor + rimBandFactor;
        const float minimum = -chunk.settings.maxDepth;
        const float maximum =
            chunk.settings.maxDepth * chunk.settings.rimStrength;

        const float reachX = (
            std::fabs(localTangent.x) * radiusX +
            std::fabs(localBitangent.x) * radiusZ) * reachFactor;
        const float reachZ = (
            std::fabs(localTangent.y) * radiusX +
            std::fabs(localBitangent.y) * radiusZ) * reachFactor;
        const int32_t minX = std::clamp(
            static_cast<int32_t>(std::floor(
                (localX - reachX - chunk.chunkOriginLocal.x) / cell)),
            0,
            static_cast<int32_t>(cells));
        const int32_t maxX = std::clamp(
            static_cast<int32_t>(std::ceil(
                (localX + reachX - chunk.chunkOriginLocal.x) / cell)),
            0,
            static_cast<int32_t>(cells));
        const int32_t minZ = std::clamp(
            static_cast<int32_t>(std::floor(
                (localZ - reachZ - chunk.chunkOriginLocal.y) / cell)),
            0,
            static_cast<int32_t>(cells));
        const int32_t maxZ = std::clamp(
            static_cast<int32_t>(std::ceil(
                (localZ + reachZ - chunk.chunkOriginLocal.y) / cell)),
            0,
            static_cast<int32_t>(cells));

        bool changed = false;
        for (int32_t z = minZ; z <= maxZ; ++z) {
            for (int32_t x = minX; x <= maxX; ++x) {
                const std::size_t vertexIndex =
                    static_cast<std::size_t>(z) * stride +
                    static_cast<uint32_t>(x);
                if (chunk.coverage[vertexIndex] <
                    SAND::kSandSurfaceVisibleCoverageThreshold) {
                    continue;
                }
                const float deformableMask =
                    vertexIndex < chunk.deformableMask.size()
                        ? std::clamp(
                            chunk.deformableMask[vertexIndex], 0.0f, 1.0f)
                        : 1.0f;
                if (!std::isfinite(deformableMask) ||
                    deformableMask <= kDeformableMaskClearThreshold) {
                    // RebuildCombinedHeights が final response へ mask を一度だけ
                    // 適用する。ここでは 0 領域の gate だけにし、imprint にも
                    // 掛けると混合境界を二重に mask してしまう。
                    continue;
                }
                const float vx = chunk.chunkOriginLocal.x +
                    static_cast<float>(x) * cell;
                const float vz = chunk.chunkOriginLocal.y +
                    static_cast<float>(z) * cell;
                const float dx = vx - localX;
                const float dz = vz - localZ;
                float radiusFactor = 0.0f;
                if (!EvaluateFootprintFactor(
                        footprintKind,
                        dx,
                        dz,
                        localTangent,
                        localBitangent,
                        radiusX,
                        radiusZ,
                        radiusFactor)) {
                    continue;
                }
                if (radiusFactor > reachFactor) {
                    continue;
                }

                // sleeping Dynamic / 停止 Kinematic は active contact を
                // 保持するだけで、新しい塑性量を追加しない。
                chunk.recoveryHold[vertexIndex] = (std::max)(
                    chunk.recoveryHold[vertexIndex],
                    chunk.settings.recoveryDelaySeconds);
                if (holdOnly) {
                    continue;
                }

                float imprint = 0.0f;
                if (radiusFactor <= capFactor) {
                    if (footprintKind ==
                            TERRAIN::TerrainFootprintKind::Sphere) {
                        imprint = (std::max)(
                            0.0f,
                            verticalRadius * std::sqrt((std::max)(
                                1.0f - radiusFactor * radiusFactor,
                                0.0f)) -
                                (verticalRadius - depth));
                    } else {
                        const float softness = std::clamp(
                            edgeSoftness, 0.0f, 1.0f);
                        const float exponent = std::lerp(
                            2.2f, 0.55f, softness);
                        imprint = depth * std::pow(
                            (std::max)(
                                1.0f - radiusFactor * radiusFactor,
                                0.0f),
                            exponent);
                    }
                } else if (rimBandFactor > 0.0f) {
                    const float t =
                        (radiusFactor - capFactor) / rimBandFactor;
                    imprint = -chunk.settings.rimStrength * depth *
                        0.5f * (1.0f - t) * (1.0f - t);
                }

                float& elastic =
                    chunk.elasticHeights[vertexIndex];
                float& plastic =
                    chunk.plasticHeights[vertexIndex];
                const float height = elastic + plastic;
                const float desired = std::clamp(
                    -imprint, minimum, maximum);
                const float next =
                    TERRAIN::AccumulateTerrainSurfaceDisplacement(
                        height,
                        desired,
                        imprint >= 0.0f
                            ? TERRAIN::TerrainSurfaceDisplacementKind::
                                Depression
                            : TERRAIN::TerrainSurfaceDisplacementKind::Rim);
                if (std::fabs(next - height) > 1.0e-6f) {
                    const float delta = next - height;
                    const float plasticShare = std::clamp(
                        chunk.settings.plasticity, 0.0f, 1.0f);
                    const float targetPlastic = next * plasticShare;
                    const float plasticDelta = delta < 0.0f
                        ? std::clamp(
                            targetPlastic - plastic,
                            delta,
                            0.0f)
                        : std::clamp(
                            targetPlastic - plastic,
                            0.0f,
                            delta);
                    plastic += plasticDelta;
                    elastic += delta - plasticDelta;
                    changed = true;
                }
            }
        }
        if (changed) {
            chunk.responseGridActive = true;
            chunk.hasDeformation = true;
            chunk.needsRelaxation = true;
            RebuildCombinedHeights(chunk);
            ++chunk.stateRevision;
        }
        return changed;
    }

    bool SandSurfaceSystem::RelaxChunk(
        SAND::SandChunkRuntimeState& chunk,
        float fixedDt) {
        if (!chunk.hasDeformation) {
            chunk.needsRelaxation = false;
            return false;
        }
        const uint32_t cells = (std::max)(chunk.cells, 1u);
        const uint32_t stride = cells + 1u;
        const std::size_t vertexCount =
            static_cast<std::size_t>(stride) * stride;
        if (chunk.deformHeights.size() != vertexCount ||
            chunk.elasticHeights.size() != vertexCount ||
            chunk.plasticHeights.size() != vertexCount ||
            chunk.recoveryHold.size() != vertexCount ||
            chunk.elasticScratch.size() != vertexCount) {
            return false;
        }

        bool changed = false;
        const float safeDt = (std::max)(fixedDt, 0.0f);
        const float elasticKeep = std::exp(
            -(std::max)(chunk.settings.refillPerSecond, 0.0f) *
                safeDt);
        const float plasticKeep = std::exp(
            -(std::max)(
                chunk.settings.plasticRefillPerSecond, 0.0f) *
                safeDt);
        const auto maskAt = [&](std::size_t index) noexcept {
            if (chunk.deformableMask.size() != vertexCount) {
                return 1.0f;
            }
            const float value = chunk.deformableMask[index];
            return std::isfinite(value)
                ? std::clamp(value, 0.0f, 1.0f)
                : 0.0f;
        };
        for (std::size_t index = 0u;
            index < vertexCount;
            ++index) {
            float& elastic = chunk.elasticHeights[index];
            float& plastic = chunk.plasticHeights[index];
            if (maskAt(index) <= kDeformableMaskClearThreshold) {
                changed = changed ||
                    std::fabs(elastic) > kRecoverySnapEpsilon ||
                    std::fabs(plastic) > kRecoverySnapEpsilon ||
                    chunk.recoveryHold[index] > 0.0f;
                elastic = 0.0f;
                plastic = 0.0f;
                chunk.recoveryHold[index] = 0.0f;
                continue;
            }
            const auto snapRecovered = [](float value) noexcept {
                return std::isfinite(value) &&
                    std::fabs(value) > kRecoverySnapEpsilon
                    ? value
                    : 0.0f;
            };
            const float snappedElastic = snapRecovered(elastic);
            const float snappedPlastic = snapRecovered(plastic);
            changed = changed ||
                snappedElastic != elastic || snappedPlastic != plastic;
            elastic = snappedElastic;
            plastic = snappedPlastic;

            float& hold = chunk.recoveryHold[index];
            if (hold > 0.0f) {
                const float nextHold = (std::max)(hold - safeDt, 0.0f);
                hold = nextHold;
                continue;
            }
            const float nextElastic = snapRecovered(
                elastic * elasticKeep);
            const float nextPlastic = snapRecovered(
                plastic * plasticKeep);
            changed = changed ||
                nextElastic != elastic || nextPlastic != plastic;
            elastic = nextElastic;
            plastic = nextPlastic;
        }

        const float diffusion = std::clamp(
            1.0f - std::exp(
                -(std::max)(
                    chunk.settings.diffusionPerSecond, 0.0f) * safeDt),
            0.0f,
            0.25f);
        const bool hasElastic = std::any_of(
            chunk.elasticHeights.begin(),
            chunk.elasticHeights.end(),
            [](float value) {
                return std::fabs(value) > kRecoverySnapEpsilon;
            });
        if (diffusion > 1.0e-4f && hasElastic) {
            std::copy(
                chunk.elasticHeights.begin(),
                chunk.elasticHeights.end(),
                chunk.elasticScratch.begin());
            const std::vector<float>& source = chunk.elasticScratch;
            const float keep = 1.0f - diffusion;
            const float neighbor = diffusion * 0.25f;
            for (uint32_t z = 1u; z < cells; ++z) {
                for (uint32_t x = 1u; x < cells; ++x) {
                    const std::size_t index =
                        static_cast<std::size_t>(z) * stride + x;
                    if (chunk.recoveryHold[index] > 0.0f) {
                        continue;
                    }
                    const auto neighborValue = [&](std::size_t candidate) {
                        return maskAt(candidate) <=
                                kDeformableMaskClearThreshold ||
                                chunk.recoveryHold[candidate] > 0.0f
                            ? source[index]
                            : source[candidate];
                    };
                    float next =
                        source[index] * keep +
                        (neighborValue(index - stride) +
                            neighborValue(index + stride) +
                            neighborValue(index - 1u) +
                            neighborValue(index + 1u)) * neighbor;
                    if (!std::isfinite(next) ||
                        std::fabs(next) <= kRecoverySnapEpsilon) {
                        next = 0.0f;
                    }
                    changed = changed ||
                        next != chunk.elasticHeights[index];
                    chunk.elasticHeights[index] = next;
                }
            }
        }
        const float minimum = -(std::max)(
            chunk.settings.maxDepth, 0.0f);
        const float maximum = (std::max)(
            chunk.settings.maxDepth, 0.0f) * (std::max)(
                chunk.settings.rimStrength, 0.0f);
        bool hasTransientElastic = false;
        for (std::size_t index = 0u;
            index < vertexCount;
            ++index) {
            TERRAIN::TerrainSurfaceDisplacementChannels channels =
                TERRAIN::ClampTerrainSurfaceDisplacementChannels(
                    {
                        chunk.elasticHeights[index],
                        chunk.plasticHeights[index],
                    },
                    minimum,
                    maximum);
            if (chunk.recoveryHold[index] <= 0.0f) {
                // chunk 全体ではなく vertex ごとに lifecycle を閉じる。
                // seam の両側は同じ hold を共有するため、一方だけが elastic、
                // 他方だけが plastic になる状態を作らない。
                const float combined = channels.Combined();
                const float frozenResponse =
                    std::fabs(combined) > kRecoverySnapEpsilon
                    ? combined
                    : 0.0f;
                channels = { 0.0f, frozenResponse };
            } else {
                hasTransientElastic = hasTransientElastic ||
                    std::fabs(channels.elastic) > kRecoverySnapEpsilon;
            }
            changed = changed ||
                channels.elastic != chunk.elasticHeights[index] ||
                channels.plastic != chunk.plasticHeights[index];
            chunk.elasticHeights[index] = channels.elastic;
            chunk.plasticHeights[index] = channels.plastic;
        }

        const bool remains = std::any_of(
                chunk.elasticHeights.begin(),
                chunk.elasticHeights.end(),
                [](float value) {
                    return std::fabs(value) > kRecoverySnapEpsilon;
                }) ||
            std::any_of(
                chunk.plasticHeights.begin(),
                chunk.plasticHeights.end(),
                [](float value) {
                    return std::fabs(value) > kRecoverySnapEpsilon;
                });
        const bool lifecycleChanged =
            chunk.hasDeformation != remains;
        if (changed) {
            RebuildCombinedHeights(chunk);
        } else {
            chunk.hasDeformation = remains;
        }
        chunk.needsRelaxation = remains && hasTransientElastic;
        if (changed || lifecycleChanged) {
            ++chunk.stateRevision;
            return true;
        }
        return false;
    }

} // namespace HIKARI
