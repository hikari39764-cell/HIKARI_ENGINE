#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <tuple>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::REFLECTION {

    namespace {

        std::vector<ReflectionProbeRuntimeData> gProbes{};
        ReflectionProbeViewSelection gViewSelection{};
        int gSamplingSuppressDepth = 0;
        std::string gLastSelectionKey{};

        bool HasUsableBoxSize(const MATH::Vec3& size) {
            return size.x > 0.001f &&
                size.y > 0.001f &&
                size.z > 0.001f;
        }

        MATH::Vec3 SanitizeBoxSize(const MATH::Vec3& size) {
            return {
                (std::max)(0.0f, size.x),
                (std::max)(0.0f, size.y),
                (std::max)(0.0f, size.z)
            };
        }

        void RefreshResolvedHandles(ReflectionProbeRuntimeData& probe) {
            probe.prefilteredHandle =
                RENDER3D::GetTextureResourceBackendHandle(
                    probe.prefilteredResource);
            probe.brdfLutHandle =
                RENDER3D::GetTextureResourceBackendHandle(
                    probe.brdfLutResource);
            probe.prefilteredSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(
                    probe.prefilteredResource);
            probe.brdfLutSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(
                    probe.brdfLutResource);
            probe.prefilteredActualMipCount =
                RENDER3D::GetTextureResourceMipCount(
                    probe.prefilteredResource);
            probe.brdfLutMipCount =
                RENDER3D::GetTextureResourceMipCount(
                    probe.brdfLutResource);
            probe.prefilteredFormat =
                RENDER3D::GetTextureResourceFormat(
                    probe.prefilteredResource);
            probe.brdfLutFormat =
                RENDER3D::GetTextureResourceFormat(
                    probe.brdfLutResource);

            probe.hasPrefiltered =
                probe.prefilteredSrv.ptr != 0 &&
                RENDER3D::GetTextureResourceDimension(
                    probe.prefilteredResource) ==
                    RENDER3D::TextureResourceDimension::TextureCube &&
                probe.prefilteredActualMipCount >= 2u;
            probe.hasBrdfLut =
                probe.brdfLutSrv.ptr != 0 &&
                RENDER3D::GetTextureResourceDimension(
                    probe.brdfLutResource) ==
                    RENDER3D::TextureResourceDimension::Texture2D &&
                probe.brdfLutMipCount >= 1u;

            const uint32_t requestedMipCount = (std::max)(
                1u,
                probe.requestedPrefilteredMipCount);
            probe.prefilteredMipCount = probe.hasPrefiltered
                ? (std::max)(
                    1u,
                    (std::min)(
                        requestedMipCount,
                        probe.prefilteredActualMipCount))
                : 1u;
            probe.prefilteredMipMismatch =
                probe.hasPrefiltered &&
                requestedMipCount != probe.prefilteredActualMipCount;

            probe.radius = (std::max)(0.0f, probe.radius);
            probe.intensity = (std::max)(0.0f, probe.intensity);
            probe.influenceBoxSize =
                SanitizeBoxSize(probe.influenceBoxSize);
            probe.projectionBoxSize =
                SanitizeBoxSize(probe.projectionBoxSize);
            probe.blendDistance =
                (std::max)(0.0f, probe.blendDistance);

            const bool influenceBoxValid =
                probe.influenceShape !=
                    RuntimeReflectionProbeInfluenceShape::Box ||
                HasUsableBoxSize(probe.influenceBoxSize);
            const bool projectionBoxValid =
                probe.projectionShape !=
                    RuntimeReflectionProbeProjectionShape::Box ||
                HasUsableBoxSize(probe.projectionBoxSize);
            probe.valid =
                probe.enabled &&
                probe.hasPrefiltered &&
                probe.radius > 0.001f &&
                probe.intensity > 0.0f &&
                influenceBoxValid &&
                projectionBoxValid;
        }

        float SmoothWeight(float value) {
            const float x = std::clamp(value, 0.0f, 1.0f);
            return x * x * (3.0f - 2.0f * x);
        }

        float SphereInfluence(
            const ReflectionProbeRuntimeData& probe,
            const MATH::Vec3& point) {

            const float dx = point.x - probe.position.x;
            const float dy = point.y - probe.position.y;
            const float dz = point.z - probe.position.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float radius = (std::max)(0.001f, probe.radius);
            const float blend = (std::min)(
                (std::max)(0.001f, probe.blendDistance),
                radius);
            return SmoothWeight((radius - distance) / blend);
        }

        float BoxInfluence(
            const ReflectionProbeRuntimeData& probe,
            const MATH::Vec3& point) {

            const MATH::Vec3 half{
                probe.influenceBoxSize.x * 0.5f,
                probe.influenceBoxSize.y * 0.5f,
                probe.influenceBoxSize.z * 0.5f
            };
            const float edgeDistance = (std::min)({
                half.x - std::abs(point.x - probe.influenceBoxCenter.x),
                half.y - std::abs(point.y - probe.influenceBoxCenter.y),
                half.z - std::abs(point.z - probe.influenceBoxCenter.z)
            });
            const float blend = (std::max)(
                0.001f,
                (std::min)(
                    probe.blendDistance,
                    (std::min)({ half.x, half.y, half.z })));
            return SmoothWeight(edgeDistance / blend);
        }

        float EvaluateInfluence(
            const ReflectionProbeRuntimeData& probe,
            const MATH::Vec3& point) {

            return probe.influenceShape ==
                    RuntimeReflectionProbeInfluenceShape::Box
                ? BoxInfluence(probe, point)
                : SphereInfluence(probe, point);
        }

        float DistanceSquared(
            const ReflectionProbeRuntimeData& probe,
            const MATH::Vec3& point) {

            const MATH::Vec3 center =
                probe.influenceShape ==
                    RuntimeReflectionProbeInfluenceShape::Box
                ? probe.influenceBoxCenter
                : probe.position;
            const float dx = point.x - center.x;
            const float dy = point.y - center.y;
            const float dz = point.z - center.z;
            return dx * dx + dy * dy + dz * dz;
        }

        void LogSelectionIfChanged() {
            std::ostringstream key{};
            key << gProbes.size() << ":";
            for (size_t slot = 0; slot < gViewSelection.count; ++slot) {
                const size_t index = gViewSelection.providerIndices[slot];
                key << gProbes[index].sourceAssetId << ";";
            }
            if (key.str() == gLastSelectionKey) {
                return;
            }
            gLastSelectionKey = key.str();
            HIKARI_LOG_INFO(
                "[Environment][ReflectionProbeSet] resident=" +
                std::to_string(gProbes.size()) +
                " selected=" +
                std::to_string(gViewSelection.count) +
                " key=" + gLastSelectionKey);
        }

    } // namespace

    void Reset() {
        gProbes.clear();
        gViewSelection = {};
        gLastSelectionKey.clear();
    }

    void SetProbeSet(std::vector<ReflectionProbeRuntimeData> probes) {
        for (ReflectionProbeRuntimeData& probe : probes) {
            RefreshResolvedHandles(probe);
            if (probe.enabled && !probe.valid) {
                HIKARI_LOG_WARN(
                    "[Environment][ReflectionProbeSet] invalid provider id=" +
                    probe.sourceAssetId +
                    " path=" + probe.prefilteredPath);
            }
        }
        std::stable_sort(
            probes.begin(),
            probes.end(),
            [](const ReflectionProbeRuntimeData& lhs,
               const ReflectionProbeRuntimeData& rhs) {
                if (lhs.priority != rhs.priority) {
                    return lhs.priority > rhs.priority;
                }
                return lhs.sourceAssetId < rhs.sourceAssetId;
            });
        gProbes = std::move(probes);
        gViewSelection = {};
        gLastSelectionKey.clear();
    }

    const std::vector<ReflectionProbeRuntimeData>& GetProbeSet() {
        for (ReflectionProbeRuntimeData& probe : gProbes) {
            RefreshResolvedHandles(probe);
        }
        return gProbes;
    }

    void PrepareView(
        const MATH::Vec3& cameraPosition,
        const MATH::Vec3& viewFocusPosition) {
        const ReflectionProbeViewSelection previousSelection =
            gViewSelection;
        gViewSelection = {};
        if (IsReflectionProbeSamplingSuppressed()) {
            LogSelectionIfChanged();
            return;
        }

        struct Candidate {
            size_t index = 0;
            float influence = 0.0f;
            float distanceSquared = std::numeric_limits<float>::max();
        };

        std::vector<Candidate> candidates{};
        for (size_t index = 0; index < gProbes.size(); ++index) {
            ReflectionProbeRuntimeData& probe = gProbes[index];
            RefreshResolvedHandles(probe);
            if (!probe.valid) {
                continue;
            }
            candidates.push_back(Candidate{
                index,
                (std::max)(
                    EvaluateInfluence(
                        probe,
                        cameraPosition),
                    EvaluateInfluence(
                        probe,
                        viewFocusPosition)),
                (std::min)(
                    DistanceSquared(
                        probe,
                        cameraPosition),
                    DistanceSquared(
                        probe,
                        viewFocusPosition))
            });
        }
        std::stable_sort(
            candidates.begin(),
            candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
                const bool lhsAffectsView = lhs.influence > 0.0001f;
                const bool rhsAffectsView = rhs.influence > 0.0001f;
                if (lhsAffectsView != rhsAffectsView) {
                    return lhsAffectsView;
                }
                const ReflectionProbeRuntimeData& lhsProbe =
                    gProbes[lhs.index];
                const ReflectionProbeRuntimeData& rhsProbe =
                    gProbes[rhs.index];
                if (lhsProbe.priority != rhsProbe.priority) {
                    return lhsProbe.priority > rhsProbe.priority;
                }
                if (lhs.influence != rhs.influence) {
                    return lhs.influence > rhs.influence;
                }
                if (lhs.distanceSquared != rhs.distanceSquared) {
                    return lhs.distanceSquared < rhs.distanceSquared;
                }
                return lhsProbe.sourceAssetId <
                    rhsProbe.sourceAssetId;
            });

        std::vector<Candidate> selected{};
        selected.reserve(kReflectionProbeViewSlotCount);
        if (!candidates.empty()) {
            selected.push_back(candidates.front());
        }
        constexpr float kRetainInfluence = 0.05f;
        for (size_t previousSlot = 0;
             previousSlot < previousSelection.count &&
                selected.size() <
                    kReflectionProbeViewSlotCount;
             ++previousSlot) {
            const size_t previousIndex =
                previousSelection.providerIndices[
                    previousSlot];
            const auto found = std::find_if(
                candidates.begin(),
                candidates.end(),
                [previousIndex](const Candidate& candidate) {
                    return candidate.index == previousIndex;
                });
            const bool alreadySelected =
                std::any_of(
                    selected.begin(),
                    selected.end(),
                    [previousIndex](
                        const Candidate& candidate) {
                        return candidate.index ==
                            previousIndex;
                    });
            if (found != candidates.end() &&
                found->influence >= kRetainInfluence &&
                !alreadySelected) {
                selected.push_back(*found);
            }
        }
        for (const Candidate& candidate : candidates) {
            if (selected.size() >=
                kReflectionProbeViewSlotCount) {
                break;
            }
            const bool alreadySelected =
                std::any_of(
                    selected.begin(),
                    selected.end(),
                    [&candidate](
                        const Candidate& current) {
                        return current.index ==
                            candidate.index;
                    });
            if (!alreadySelected) {
                selected.push_back(candidate);
            }
        }

        gViewSelection.count = selected.size();
        for (size_t slot = 0; slot < gViewSelection.count; ++slot) {
            gViewSelection.providerIndices[slot] =
                selected[slot].index;
            gViewSelection.viewInfluences[slot] =
                selected[slot].influence;
        }
        LogSelectionIfChanged();
    }

    const ReflectionProbeViewSelection& GetViewSelection() {
        return gViewSelection;
    }

    const ReflectionProbeRuntimeData* GetViewProbe(size_t slot) {
        if (slot >= gViewSelection.count) {
            return nullptr;
        }
        const size_t index = gViewSelection.providerIndices[slot];
        return index < gProbes.size() ? &gProbes[index] : nullptr;
    }

    bool IsReflectionProbeSamplingSuppressed() {
        return gSamplingSuppressDepth > 0;
    }

    ScopedReflectionProbeSamplingSuppress::
        ScopedReflectionProbeSamplingSuppress() {
        ++gSamplingSuppressDepth;
    }

    ScopedReflectionProbeSamplingSuppress::
        ~ScopedReflectionProbeSamplingSuppress() {
        gSamplingSuppressDepth =
            (std::max)(0, gSamplingSuppressDepth - 1);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetViewPrefilteredSrv(size_t slot) {
        if (IsReflectionProbeSamplingSuppressed()) {
            return {};
        }
        const ReflectionProbeRuntimeData* probe = GetViewProbe(slot);
        return probe != nullptr && probe->valid
            ? probe->prefilteredSrv
            : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetSharedBrdfLutSrv() {
        if (IsReflectionProbeSamplingSuppressed()) {
            return {};
        }
        for (const ReflectionProbeRuntimeData& probe : gProbes) {
            if (probe.valid && probe.hasBrdfLut &&
                probe.brdfLutSrv.ptr != 0) {
                return probe.brdfLutSrv;
            }
        }
        return {};
    }

} // namespace HIKARI::REFLECTION
