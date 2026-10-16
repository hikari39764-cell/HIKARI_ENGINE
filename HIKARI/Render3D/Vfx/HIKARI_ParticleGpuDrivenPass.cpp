#include "Render3D/Vfx/HIKARI_ParticleGpuDrivenPass.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include "Render3D/Vfx/HIKARI_ParticleCapacity.h"
#include "Render3D/Vfx/HIKARI_ParticleGeometryLayout.h"
#include "Render3D/Vfx/HIKARI_ParticleCompositionRenderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Vfx/HIKARI_VfxProgram.h"
#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Vfx/HIKARI_GpuParticleParameterPacking.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleShaderInterop.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleStorage.h"
#include "Render3D/Vfx/Internal/HIKARI_ParticleGpuDrivenPipelineState.h"
#include "Render3D/Vfx/Internal/HIKARI_ParticleMaterialResolver.h"

namespace HIKARI::RENDER3D::PARTICLES {
namespace {
using INTERNAL::BuildGpuParticleEmitterParameters;
using INTERNAL::GpuParticleCompactConstants;
using INTERNAL::GpuParticleDrawConstants;
using INTERNAL::GpuParticleEmitterParameters;
using INTERNAL::GpuParticleShapeState;
using INTERNAL::GpuParticleSimulationConstants;
using INTERNAL::MakeGpuParticleFloat4;
using INTERNAL::ResolvedParticleMaterial;

inline constexpr uint32_t kThreadGroupSize = kParticleAllocationGranularity;

struct ParticleEmitterInstanceKeyHasher {
    size_t operator()(const ParticleEmitterInstanceKey& key) const noexcept {
        size_t value = static_cast<size_t>(key.playbackNamespace);
        const auto mix = [&](uint64_t next) {
            value ^= static_cast<size_t>(next) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
        };
        mix(key.playbackSlot);
        mix(key.playbackGeneration);
        mix(key.emitterIndex);
        return value;
    }
};

struct FreeRange {
    uint32_t offset = 0u;
    uint32_t count = 0u;
};

struct EmitterAllocation {
    uint32_t offset = 0u;
    uint32_t reservedCapacity = 0u;
    uint32_t activeCapacity = 0u;
    uint32_t spawnCursor = 0u;
    bool resetPending = true;
    uint64_t lastSimulatedFrame = (std::numeric_limits<uint64_t>::max)();
    uint32_t highestSimulationOrdinal = 0u;
    bool simulatedThisFrame = false;
};

struct DrawItem {
    ParticleEmitterInstanceKey key{};
    ParticleEmitterRenderParameters render{};
    MATH::Vec3 worldPosition{0.0f, 0.0f, 0.0f};
    uint32_t particleOffset = 0u;
    uint32_t particleCapacity = 0u;
    uint32_t aliveIndexBase = 0u;
    uint32_t materialIndex = 0u;
    uint32_t drawIndex = 0u;
    bool materialReady = false;
    ResolvedParticleMaterial resolvedMaterial{};
};

struct DrawOrderEntry {
    uint32_t itemIndex = 0u;
    float cameraDistanceSquared = 0.0f;
};

uint32_t AlignParticleCount(uint32_t value) noexcept {
    const auto aligned = AlignParticleCapacity(value);
    return aligned <= UINT32_MAX ? static_cast<uint32_t>(aligned) : 0u;
}

uint32_t DispatchGroupCount(uint32_t value) noexcept { return (value + kThreadGroupSize - 1u) / kThreadGroupSize; }

uint64_t HashCombine(uint64_t seed, uint64_t value) noexcept {
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
}

uint64_t HashEmitterKey(const ParticleEmitterInstanceKey& key) noexcept {
    uint64_t value = key.playbackNamespace;
    value = HashCombine(value, key.playbackSlot);
    value = HashCombine(value, key.playbackGeneration);
    return HashCombine(value, key.emitterIndex);
}

// 形状（ジオメトリ）段が読む per-draw パラメータを組み立てる唯一の実装。
GpuParticleShapeState BuildShapeState(const ParticleEmitterRenderParameters& render) {
    GpuParticleShapeState state{};
    state.renderShapeParamsA = MakeGpuParticleFloat4(render.ribbonWidth, render.ringOuterRadius, render.ringInnerRadius,
                                                     render.ringStartAngleRadians);
    // 変形パラメータは専用フィールド（バッファに余裕があるため
    // 形状ごとのスロット使い回しは不要）。
    state.bendAmount = render.bendAmount;
    state.twistRadians = render.twistRadians;
    state.renderUvParams = MakeGpuParticleFloat4((std::max)(render.uvTileLength, 1.0e-4f), render.uvTilingU,
                                                 render.uvTilingV, render.uvRotationRadians);
    state.renderAnchor = MakeGpuParticleFloat4(render.anchor.x, render.anchor.y, render.anchor.z, 0.0f);
    state.renderUvMode = (render.uvMode == 1u ? 1u : 0u) | (render.uvSwapAxes != 0u ? 2u : 0u);
    state.shapeParam0 = std::bit_cast<uint32_t>(render.cylinderTopRadius);
    state.shapeParam1 = std::bit_cast<uint32_t>(render.cylinderBottomRadius);
    state.shapeParam2 = std::bit_cast<uint32_t>(render.coneBaseRadius);
    state.shapeParam3 = render.torusMajorRadius;
    state.shapeParam4 = render.torusMinorRadius;
    state.shapeParam5 = render.capsuleRadius;
    state.billboardAxis = static_cast<float>(render.billboardAxis);
    state.spatialCount = static_cast<uint32_t>((std::min)(render.spatialEffects.size(), state.spatialEffects.size()));
    const auto pack = [](const MATH::Vec4& v) { return MakeGpuParticleFloat4(v.x, v.y, v.z, v.w); };
    for (uint32_t i = 0; i < state.spatialCount; ++i) {
        const auto& effect = render.spatialEffects[i];
        auto& gpu = state.spatialEffects[i];
        gpu.metadata =
            MakeGpuParticleFloat4(float(effect.kind), effect.mix, float(effect.mask), effect.maskInvert ? 1.0f : 0.0f);
        gpu.parameters = pack(effect.parameters);
        gpu.parameters2 = pack(effect.parameters2);
        gpu.maskParameters = pack(effect.maskParameters);
        gpu.origin = MakeGpuParticleFloat4(effect.origin.x, effect.origin.y, effect.origin.z, 0);
        gpu.rotation =
            MakeGpuParticleFloat4(effect.rotation.x, effect.rotation.y, effect.rotation.z, effect.rotation.w);
        gpu.scale = MakeGpuParticleFloat4(effect.scale.x, effect.scale.y, effect.scale.z, 0);
    }
    return state;
}
} // namespace

struct ParticleGpuDrivenPass::Impl {
    GFX::Context context{};
    INTERNAL::GpuParticleStorage storage{};
    INTERNAL::ParticleGpuDrivenPipelineState pipelines{};
    INTERNAL::ParticleMaterialResolver materialResolver{};
    std::unordered_map<ParticleEmitterInstanceKey, EmitterAllocation, ParticleEmitterInstanceKeyHasher> allocations{};
    std::vector<FreeRange> freeRanges{};
    std::vector<ParticleComposition> compositions;
    ParticleCompositionRenderer compositionRenderer;
    std::unordered_map<ParticleEmitterInstanceKey, size_t, ParticleEmitterInstanceKeyHasher> compositionIndices;
    std::unordered_map<ParticleEmitterInstanceKey, uint32_t, ParticleEmitterInstanceKeyHasher> itemIndices;
    std::vector<ParticleEmitterInstanceKey> compositionLookupKeys, itemLookupKeys;
    std::vector<bool> renderedCompositions;
    std::vector<DrawItem> drawItems{};
    std::vector<DrawOrderEntry> drawOrder{};
    std::unordered_set<std::string> residentStandardTextures{};
    uint64_t prewarmedRegistryId = 0u;
    uint64_t prewarmedRegistryRevision = 0u;
    ParticleGpuDrivenPassStats stats{};
    uint32_t emitterPageCapacity = 0u;
    uint32_t emitterUploadCapacity = 0u;
    uint32_t emitterUploadCursor = 0u;
    uint32_t shapeUploadCursor = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS preparedShapeAddress = 0u;
    uint32_t uploadFrameSlot = (std::numeric_limits<uint32_t>::max)();
    uint64_t uploadFrameSerial = (std::numeric_limits<uint64_t>::max)();
    uint64_t lastCompactionLayoutVersion = 0u;
    uint64_t materialSyncRetryVersion = 0u;
    bool drawBuffersValid = false;
    bool initialized = false;
    bool initializationAttempted = false;

    void ResetFrameStats() noexcept {
        stats.simulatedEmitters = 0u;
        stats.simulationSkippedEmitters = 0u;
        stats.compactedEmitters = 0u;
        stats.compactionReuseFrames = 0u;
        stats.drawnEmitters = 0u;
        stats.spawnedParticles = 0u;
        stats.simulationFailures = 0u;
        stats.materialFailures = 0u;
        stats.compactedCapacityThreads = 0u;
        stats.indirectDispatchCommands = 0u;
        stats.spriteDispatchCommands = 0u;
        stats.stripDispatchCommands = 0u;
        stats.inputAccepted = false;
        stats.simulationAccepted = false;
        stats.simulationAcceptedToken = {};
    }

    bool EnsureStandardTextureResidency(const ParticleRenderFrameSource& source, const ParticleRenderInput& input) {
        if (source.assetRegistry == nullptr) {
            return std::none_of(input.emitters.begin(), input.emitters.end(),
                                [](const ParticleEmitterSubmission& submission) {
                                    return submission.drawAfterSimulation &&
                                           (IsCustomParticleMaterial(submission.render.materialMode) ||
                                            !submission.render.colorTextureAssetGuid.empty());
                                });
        }

        std::vector<TextureResourceLoadRequest> requests{};
        std::vector<std::string> residencyKeys{};
        std::unordered_set<std::string> queuedResidencyKeys{};
        std::unordered_set<std::string> requiredGuids{};
        // 一つの破損した VFX が、他の正常な依存資源の予熱まで妨げないようにする。
        bool dependenciesValid = true;

        const auto queueTexture = [&source, this, &requests, &residencyKeys, &queuedResidencyKeys](
                                      std::string_view guid, std::string name, TextureResourceColorSpace colorSpace) {
            const auto* descriptor = source.assetRegistry->FindAs<TextureAssetDescriptor>(AssetId{std::string(guid)});
            if (descriptor == nullptr || descriptor->sourcePath.empty()) {
                HIKARI_LOG_ERROR("[ParticleGpuDrivenPass][TexturePreloadMissing] " + std::string(guid));
                return false;
            }
            const std::string residencyKey = std::to_string(source.assetRegistry->GetInstanceId()) + ":" +
                                             std::to_string(source.assetRegistry->GetRevision()) + ":" +
                                             std::string(guid) + ":" + descriptor->sourcePath + ":" +
                                             std::to_string(static_cast<uint32_t>(colorSpace));
            if (residentStandardTextures.contains(residencyKey) || !queuedResidencyKeys.insert(residencyKey).second) {
                return true;
            }
            requests.push_back({
                std::move(name),
                descriptor->sourcePath,
                colorSpace,
            });
            residencyKeys.push_back(residencyKey);
            return true;
        };

        for (const ParticleEmitterSubmission& submission : input.emitters) {
            if (!submission.drawAfterSimulation || IsCustomParticleMaterial(submission.render.materialMode) ||
                submission.render.colorTextureAssetGuid.empty()) {
                continue;
            }
            requiredGuids.insert(submission.render.colorTextureAssetGuid);
        }

        for (const std::string& guid : requiredGuids) {
            if (!queueTexture(guid, "material_asset/standard:" + guid + "/baseColor",
                              MaterialTextureColorSpace(MaterialTextureUsage::BaseColor))) {
                dependenciesValid = false;
            }
        }

        std::unordered_set<std::string> customMaterialGuids{};
        for (const ParticleEmitterSubmission& submission : input.emitters) {
            if (!submission.drawAfterSimulation || !IsCustomParticleMaterial(submission.render.materialMode) ||
                submission.render.materialAssetGuid.empty() ||
                !customMaterialGuids.insert(submission.render.materialAssetGuid).second) {
                continue;
            }
            const std::string& materialGuid = submission.render.materialAssetGuid;
            const auto* material = source.assetRegistry->FindAs<MaterialAssetDescriptor>(AssetId{materialGuid});
            if (material == nullptr || !material->compileResult.success) {
                HIKARI_LOG_ERROR("[ParticleGpuDrivenPass]"
                                 "[MaterialPreloadMissing] " +
                                 materialGuid);
                dependenciesValid = false;
                continue;
            }
            for (const MaterialProgramResourceBinding& binding : material->compileResult.program.resources) {
                if (!binding.assetGuid.IsValid() || binding.type != MaterialResourceType::Texture2D ||
                    binding.semantic == MaterialResourceSemantic::Ramp) {
                    continue;
                }
                const bool srgb = binding.semantic == MaterialResourceSemantic::BaseColor ||
                                  binding.semantic == MaterialResourceSemantic::Emissive ||
                                  binding.semantic == MaterialResourceSemantic::SpecularColor ||
                                  binding.semantic == MaterialResourceSemantic::MatCap;
                if (!queueTexture(binding.assetGuid.value,
                                  "material_program/" + materialGuid + "/" + binding.resourceId,
                                  srgb ? TextureResourceColorSpace::Srgb : TextureResourceColorSpace::Linear)) {
                    dependenciesValid = false;
                }
            }
        }
        if (requests.empty())
            return dependenciesValid;

        TextureResourceBatchLoadStats loadStats{};
        const std::vector<TextureResourceHandle> handles = PreloadTextureResourcesWithColorSpace(requests, &loadStats);
        bool resident = handles.size() == requests.size();
        const size_t count = (std::min)(handles.size(), residencyKeys.size());
        for (size_t index = 0u; index < count; ++index) {
            if (GetTextureResourceBackendHandle(handles[index]) < 0) {
                resident = false;
                continue;
            }
            residentStandardTextures.insert(residencyKeys[index]);
        }
        if (!resident) {
            HIKARI_LOG_ERROR("[ParticleGpuDrivenPass][TexturePreloadFailed] requested=" +
                             std::to_string(loadStats.requested) + " failed=" + std::to_string(loadStats.failed));
        }
        return resident && dependenciesValid;
    }

    void CoalesceFreeRanges() {
        std::sort(freeRanges.begin(), freeRanges.end(),
                  [](const FreeRange& lhs, const FreeRange& rhs) { return lhs.offset < rhs.offset; });
        std::vector<FreeRange> merged{};
        merged.reserve(freeRanges.size());
        for (const FreeRange& range : freeRanges) {
            if (range.count == 0u)
                continue;
            if (!merged.empty() && merged.back().offset + merged.back().count == range.offset) {
                merged.back().count += range.count;
            } else {
                merged.push_back(range);
            }
        }
        freeRanges = std::move(merged);
    }

    void ReleaseAllocation(const ParticleEmitterInstanceKey& key) {
        const auto found = allocations.find(key);
        if (found == allocations.end())
            return;
        freeRanges.push_back({found->second.offset, found->second.reservedCapacity});
        allocations.erase(found);
        drawBuffersValid = false;
        CoalesceFreeRanges();
    }

    void ReleaseNamespace(uint64_t playbackNamespace) {
        if (playbackNamespace == 0u)
            return;
        for (auto allocation = allocations.begin(); allocation != allocations.end();) {
            if (allocation->first.playbackNamespace != playbackNamespace) {
                ++allocation;
                continue;
            }
            freeRanges.push_back({allocation->second.offset, allocation->second.reservedCapacity});
            allocation = allocations.erase(allocation);
        }
        std::erase_if(drawItems, [playbackNamespace](const DrawItem& item) {
            return item.key.playbackNamespace == playbackNamespace;
        });
        drawBuffersValid = false;
        CoalesceFreeRanges();
        RefreshAllocationStats();
    }

    void RefreshAllocationStats() {
        stats.activeEmitters = static_cast<uint32_t>(allocations.size());
        stats.allocatedParticles = 0u;
        for (const auto& [key, allocation] : allocations) {
            (void)key;
            stats.allocatedParticles += allocation.reservedCapacity;
        }
    }

    EmitterAllocation* AcquireAllocation(const ParticleEmitterInstanceKey& key, uint32_t requestedCapacity,
                                         bool& outputReset) {
        outputReset = false;
        const uint32_t alignedCapacity = AlignParticleCount(requestedCapacity);
        if (alignedCapacity == 0u || alignedCapacity > stats.particleCapacity) {
            ++stats.allocationFailures;
            return nullptr;
        }
        auto existing = allocations.find(key);
        if (existing != allocations.end() && existing->second.reservedCapacity >= alignedCapacity) {
            if (existing->second.activeCapacity != requestedCapacity) {
                existing->second.activeCapacity = requestedCapacity;
                existing->second.spawnCursor = 0u;
                existing->second.resetPending = true;
                outputReset = true;
            }
            return &existing->second;
        }
        if (existing != allocations.end()) {
            ReleaseAllocation(key);
            outputReset = true;
        }
        for (size_t index = 0u; index < freeRanges.size(); ++index) {
            FreeRange& range = freeRanges[index];
            if (range.count < alignedCapacity)
                continue;
            EmitterAllocation allocation{};
            allocation.offset = range.offset;
            allocation.reservedCapacity = alignedCapacity;
            allocation.activeCapacity = requestedCapacity;
            range.offset += alignedCapacity;
            range.count -= alignedCapacity;
            if (range.count == 0u) {
                freeRanges.erase(freeRanges.begin() + static_cast<std::ptrdiff_t>(index));
            }
            auto [inserted, success] = allocations.emplace(key, allocation);
            (void)success;
            outputReset = true;
            return &inserted->second;
        }
        ++stats.allocationFailures;
        return nullptr;
    }

    void DispatchSimulation(uint32_t pipelineIndex, const GpuParticleSimulationConstants& constants,
                            uint32_t threadCount, D3D12_GPU_VIRTUAL_ADDRESS emitterAddress) {
        ID3D12PipelineState* pipeline = pipelines.GetSimulationPipeline(pipelineIndex);
        if (threadCount == 0u || pipeline == nullptr || emitterAddress == 0u || context.cmdList == nullptr) {
            return;
        }
        context.cmdList->SetComputeRootSignature(pipelines.GetSimulationRootSignature());
        context.cmdList->SetPipelineState(pipeline);
        context.cmdList->SetComputeRootDescriptorTable(0u, storage.GetParticleUavGpuHandle());
        context.cmdList->SetComputeRootShaderResourceView(1u, emitterAddress);
        context.cmdList->SetComputeRoot32BitConstants(2u, sizeof(constants) / sizeof(uint32_t), &constants, 0u);
        context.cmdList->Dispatch(DispatchGroupCount(threadCount), 1u, 1u);
    }

    bool SimulateEmitter(const ParticleEmitterSubmission& submission, EmitterAllocation& allocation, bool reset,
                         uint32_t emitterUploadIndex, uint32_t frameResourceIndex) {
        const INTERNAL::GpuParticleEmitterUploadAddress address =
            INTERNAL::ResolveGpuParticleEmitterUploadAddress(emitterUploadIndex, emitterPageCapacity);
        if (!address.valid)
            return false;
        const D3D12_GPU_VIRTUAL_ADDRESS emitterAddress =
            storage.GetEmitterUploadGpuAddress(frameResourceIndex, address.pageIndex);
        if (emitterAddress == 0u)
            return false;

        GpuParticleSimulationConstants constants{};
        constants.particleOffset = allocation.offset;
        constants.particleCapacity = allocation.activeCapacity;
        constants.emitterIndex = address.elementIndex;
        constants.spawnCursor = allocation.spawnCursor;
        constants.spawnCount = (std::min)(submission.spawnCount, allocation.activeCapacity);
        constants.randomSeed = submission.randomSeed;
        constants.deltaSeconds = (std::max)(submission.deltaSeconds, 0.0f);
        if (submission.directShape) {
            constants.flags = 1u;
            DispatchSimulation(0u, constants, allocation.activeCapacity, emitterAddress);
            storage.InsertUavBarrier(context);
            ++stats.simulatedEmitters;
            return true;
        }
        if (reset || submission.resetSimulation) {
            DispatchSimulation(0u, constants, allocation.activeCapacity, emitterAddress);
            storage.InsertUavBarrier(context);
        } else if (constants.deltaSeconds > 0.0f) {
            DispatchSimulation(1u, constants, allocation.activeCapacity, emitterAddress);
            storage.InsertUavBarrier(context);
        }
        if (constants.spawnCount > 0u) {
            DispatchSimulation(2u, constants, constants.spawnCount, emitterAddress);
            storage.InsertUavBarrier(context);
            allocation.spawnCursor = (allocation.spawnCursor + constants.spawnCount) % allocation.activeCapacity;
            stats.spawnedParticles += constants.spawnCount;
        }
        ++stats.simulatedEmitters;
        return true;
    }

    bool SyncMaterials(const ParticleRenderFrameSource& source, MATERIAL::GpuMaterialRegistry& registry,
                       uint64_t layoutVersion, uint64_t sourceVersion) {
        const ParticleRenderInput& input = *source.input;
        const uintptr_t identity = MakeParticleMaterialSourceIdentity(input.sourceIdentity, source.input);
        const MATERIAL::GpuMaterialSourceSyncMode mode = registry.BeginSourceSync(
            identity, layoutVersion, sourceVersion, sourceVersion, static_cast<uint32_t>(drawItems.size()));
        if (mode == MATERIAL::GpuMaterialSourceSyncMode::None) {
            const std::span<const uint32_t> bindings = registry.GetSourceBindings(identity);
            if (bindings.size() < drawItems.size())
                return false;
            bool allResolved = true;
            for (size_t index = 0u; index < drawItems.size(); ++index) {
                drawItems[index].materialIndex = bindings[index];
                drawItems[index].materialReady = bindings[index] != MATERIAL::kInvalidGpuMaterialIndex;
                allResolved = drawItems[index].materialReady && allResolved;
            }
            return allResolved;
        }

        bool allResolved = true;
        for (uint32_t index = 0u; index < static_cast<uint32_t>(drawItems.size()); ++index) {
            DrawItem& item = drawItems[index];
            MATERIAL::GpuMaterialProjection projection{};
            if (item.resolvedMaterial.rejected) {
                projection.baseColor = {1.0f, 0.0f, 1.0f, 1.0f};
            }
            const Material* material = item.resolvedMaterial.material;
            if (material != nullptr) {
                projection = MATERIAL::BuildRuntimeMaterialGpuProjection(*material);
            }
            const auto key = INTERNAL::BuildParticleMaterialSourceKey(material, item.render, HashEmitterKey(item.key),
                                                                      identity, item.resolvedMaterial.rejected);
            item.materialIndex =
                registry.ResolveAndBindSource(index, key, MATERIAL::PackGpuMaterialData(projection), true);
            if (item.materialIndex == MATERIAL::kInvalidGpuMaterialIndex) {
                allResolved = false;
                continue;
            }
            if (material != nullptr) {
                const std::vector<uint32_t> resources =
                    MATERIAL::ResolveMaterialProgramResourceDescriptorIndices(*material);
                auto parameters = material->GetMaterialProgramParameterBlock();
                std::vector<MATH::Vec4> animated;
                if (!item.render.animatedParameters.empty()) {
                    animated.assign(parameters.begin(), parameters.end());
                    for (const auto& value : item.render.animatedParameters)
                        if (value.slot < animated.size())
                            animated[value.slot].x = value.value;
                    parameters = animated;
                }
                if (!registry.UpdateProgramBlocks(item.materialIndex, material->GetMaterialProgramId(), parameters,
                                                  resources, material->GetMaterialProgramInstanceFlags())) {
                    allResolved = false;
                    item.materialIndex = MATERIAL::kInvalidGpuMaterialIndex;
                    continue;
                }
            }
            item.materialReady = true;
        }
        registry.EndSourceSync();
        return allResolved;
    }

    void CompactDrawItems() {
        if (context.cmdList == nullptr || drawItems.empty())
            return;
        storage.Transition(context, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        storage.TransitionAliveIndices(context, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        storage.TransitionDrawCounters(context, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        storage.TransitionDrawArguments(context, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        context.cmdList->SetComputeRootSignature(pipelines.GetCompactionRootSignature());
        context.cmdList->SetComputeRootDescriptorTable(0u, storage.GetParticleUavGpuHandle());
        context.cmdList->SetComputeRootUnorderedAccessView(1u, storage.GetAliveIndexGpuAddress());
        context.cmdList->SetComputeRootUnorderedAccessView(2u, storage.GetDrawCounterGpuAddress());
        context.cmdList->SetComputeRootUnorderedAccessView(3u, storage.GetDrawArgumentGpuAddress());
        // 独立した emitter の UAV は段階ごとにまとめて同期する。
        // reset -> compact -> args の依存は保持し、emitter 間の不要な停止を除く。
        for (uint32_t phase = 0; phase < 3; ++phase) {
            context.cmdList->SetPipelineState(pipelines.GetCompactionPipeline(phase));
            for (const DrawItem& item : drawItems) {
                GpuParticleCompactConstants constants{};
                constants.particleOffset = item.particleOffset;
                constants.particleCapacity = item.particleCapacity;
                constants.drawIndex = item.drawIndex;
                constants.aliveIndexBase = item.aliveIndexBase;
                constants.vertexCount = ParticleGeometryTriangleCount(item.render) * 3u;
                context.cmdList->SetComputeRoot32BitConstants(4u, sizeof(constants) / sizeof(uint32_t), &constants, 0u);
                context.cmdList->Dispatch(phase == 1 ? DispatchGroupCount(item.particleCapacity) : 1u, 1u, 1u);
            }
            if (phase == 0)
                storage.InsertDrawResetUavBarrier(context);
            if (phase == 1)
                storage.InsertDrawCounterUavBarrier(context);
        }
        stats.compactedEmitters += static_cast<uint32_t>(drawItems.size());
        for (const auto& item : drawItems)
            stats.compactedCapacityThreads += item.particleCapacity;
        storage.Transition(context, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        storage.TransitionAliveIndices(context, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        storage.TransitionDrawCounters(context, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        storage.TransitionDrawArguments(context, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    }

    bool PrepareFrame(const GFX::Context& newContext, const ParticleRenderFrameSource* source,
                      MATERIAL::GpuMaterialRegistry& registry) {
        context = newContext;
        drawItems.clear();
        compositions.clear();
        ResetFrameStats();
        if (source == nullptr || source->input == nullptr) {
            drawBuffersValid = false;
            stats.inputAccepted = true;
            return true;
        }
        const ParticleRenderInput& input = *source->input;
        compositions = input.compositions;
        for (const ParticleEmitterInstanceKey& retired : input.retiredEmitters) {
            ReleaseAllocation(retired);
        }
        if (input.emitters.empty()) {
            drawBuffersValid = false;
            RefreshAllocationStats();
            stats.inputAccepted = true;
            stats.simulationAccepted = true;
            stats.simulationAcceptedToken = input.GetDeliveryToken();
            return true;
        }
        if (!initialized) {
            return false;
        }
        if (context.cmdList == nullptr || context.srvHeap == nullptr || !storage.IsReady()) {
            return false;
        }
        if (!EnsureStandardTextureResidency(*source, input)) {
            ++stats.materialFailures;
            RefreshAllocationStats();
            return false;
        }
        GFX::PIX::ScopedGpuEvent simulationEvent(context.cmdList, GFX::PIX::kColorRender, "Particles.SimulateCompact");
        GFX::GPU_PROFILE::ScopedGpuTimer simulationTimer(context.cmdList, GFX::GPU_PROFILE::Pass::ParticleSimulation);
        const uint64_t frameSerial = TIME::GetFrameContext().frameIndex;
        if (uploadFrameSerial != frameSerial || uploadFrameSlot != context.frameIndex) {
            uploadFrameSerial = frameSerial;
            uploadFrameSlot = context.frameIndex;
            emitterUploadCursor = 0u;
            shapeUploadCursor = 0u;
        }
        ID3D12DescriptorHeap* heaps[] = {context.srvHeap};
        context.cmdList->SetDescriptorHeaps(1u, heaps);
        const uint32_t frameResourceIndex = context.frameIndex % GFX::kFrameResourceCount;
        std::unordered_map<ParticleEmitterInstanceKey, size_t, ParticleEmitterInstanceKeyHasher> drawItemIndices{};
        uint64_t layoutVersion = 1469598103934665603ull;
        uint64_t materialVersion = 1099511628211ull;
        bool simulatedAny = false;
        bool inputAccepted = true;
        bool simulationAccepted = true;
        for (const ParticleEmitterSubmission& submission : input.emitters) {
            bool reset = false;
            EmitterAllocation* allocation = AcquireAllocation(submission.key, submission.simulation.capacity, reset);
            if (allocation == nullptr) {
                inputAccepted = false;
                simulationAccepted = false;
                continue;
            }
            if (allocation->lastSimulatedFrame != input.frameIndex) {
                allocation->lastSimulatedFrame = input.frameIndex;
                allocation->highestSimulationOrdinal = 0u;
                allocation->simulatedThisFrame = false;
            }
            const bool hasSimulationWork = reset || allocation->resetPending || submission.directShape ||
                                           submission.resetSimulation || submission.spawnCount > 0u ||
                                           submission.deltaSeconds > 0.0f;
            const bool needsSimulation =
                hasSimulationWork && (!allocation->simulatedThisFrame ||
                                      submission.simulationOrdinal > allocation->highestSimulationOrdinal);
            if (needsSimulation) {
                if (emitterUploadCursor >= emitterUploadCapacity) {
                    ++stats.simulationFailures;
                    inputAccepted = false;
                    simulationAccepted = false;
                    continue;
                }
                const uint32_t emitterUploadIndex = emitterUploadCursor++;
                storage.Transition(context, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                const INTERNAL::GpuParticleEmitterUploadAddress address =
                    INTERNAL::ResolveGpuParticleEmitterUploadAddress(emitterUploadIndex, emitterPageCapacity);
                const GpuParticleEmitterParameters parameters = BuildGpuParticleEmitterParameters(submission);
                if (!storage.WriteEmitterParameters(context.device, frameResourceIndex, address, parameters) ||
                    !SimulateEmitter(submission, *allocation, reset || allocation->resetPending, emitterUploadIndex,
                                     frameResourceIndex)) {
                    ++stats.simulationFailures;
                    inputAccepted = false;
                    simulationAccepted = false;
                    continue;
                }
                allocation->simulatedThisFrame = true;
                allocation->highestSimulationOrdinal = submission.simulationOrdinal;
                allocation->resetPending = false;
                simulatedAny = true;
            } else if (!hasSimulationWork) {
                ++stats.simulationSkippedEmitters;
            }
            if (!submission.drawAfterSimulation)
                continue;

            DrawItem item{};
            item.key = submission.key;
            item.render = submission.render;
            item.worldPosition = submission.worldPosition;
            item.particleOffset = allocation->offset;
            item.particleCapacity = allocation->activeCapacity;
            item.aliveIndexBase = allocation->offset;
            item.resolvedMaterial =
                materialResolver.Resolve(submission.render, source->assetRegistry, source->externalValues);
            const auto existing = drawItemIndices.find(submission.key);
            if (existing == drawItemIndices.end()) {
                drawItemIndices.emplace(submission.key, drawItems.size());
                drawItems.push_back(std::move(item));
            } else {
                drawItems[existing->second] = std::move(item);
            }
        }
        stats.simulationAccepted = simulationAccepted;
        if (simulationAccepted) {
            stats.simulationAcceptedToken = input.GetDeliveryToken();
        }
        if (source->assetRegistry != nullptr) {
            materialVersion = HashCombine(materialVersion, source->assetRegistry->GetInstanceId());
            materialVersion = HashCombine(materialVersion, source->assetRegistry->GetRevision());
        }
        for (uint32_t index = 0u; index < static_cast<uint32_t>(drawItems.size()); ++index) {
            DrawItem& item = drawItems[index];
            materialVersion = HashCombine(materialVersion, std::hash<std::string>{}(item.render.effectMaterialKey));
            for (const auto& value : item.render.animatedParameters) {
                const uint32_t bits = std::bit_cast<uint32_t>(value.value);
                materialVersion = HashCombine(materialVersion, value.slot);
                materialVersion = HashCombine(materialVersion, bits);
            }
            item.drawIndex = index;
            layoutVersion = HashCombine(layoutVersion, static_cast<uint32_t>(item.render.shape));
            layoutVersion = HashCombine(layoutVersion, item.render.ribbonSegmentCount);
            layoutVersion = HashCombine(layoutVersion, item.render.ringSegmentCount);
            layoutVersion = HashCombine(layoutVersion, !item.render.spatialEffects.empty());
            layoutVersion = HashCombine(layoutVersion, HashEmitterKey(item.key));
            layoutVersion = HashCombine(layoutVersion, item.particleOffset);
            layoutVersion = HashCombine(layoutVersion, item.particleCapacity);
            materialVersion = HashCombine(materialVersion, static_cast<uint64_t>(item.render.materialMode));
            const std::string_view renderResourceGuid = !IsCustomParticleMaterial(item.render.materialMode)
                                                            ? std::string_view(item.render.colorTextureAssetGuid)
                                                            : std::string_view(item.render.materialAssetGuid);
            materialVersion = HashCombine(materialVersion, std::hash<std::string_view>{}(renderResourceGuid));
            materialVersion = HashCombine(materialVersion, reinterpret_cast<uintptr_t>(item.resolvedMaterial.material));
            materialVersion = HashCombine(materialVersion, item.resolvedMaterial.material != nullptr
                                                               ? item.resolvedMaterial.material->GetRevision()
                                                               : (item.resolvedMaterial.rejected ? 2u : 1u));
        }
        materialVersion = HashCombine(materialVersion, materialSyncRetryVersion);
        if (!SyncMaterials(*source, registry, layoutVersion, materialVersion)) {
            ++stats.materialFailures;
            inputAccepted = false;
            NUMERIC::AdvanceNonZero(materialSyncRetryVersion);
            std::erase_if(drawItems, [](const DrawItem& item) { return !item.materialReady; });
            layoutVersion = 1469598103934665603ull;
            for (uint32_t index = 0u; index < static_cast<uint32_t>(drawItems.size()); ++index) {
                DrawItem& item = drawItems[index];
                item.drawIndex = index;
                layoutVersion = HashCombine(layoutVersion, static_cast<uint32_t>(item.render.shape));
                layoutVersion = HashCombine(layoutVersion, item.render.ribbonSegmentCount);
                layoutVersion = HashCombine(layoutVersion, item.render.ringSegmentCount);
                layoutVersion = HashCombine(layoutVersion, !item.render.spatialEffects.empty());
                layoutVersion = HashCombine(layoutVersion, HashEmitterKey(item.key));
                layoutVersion = HashCombine(layoutVersion, item.particleOffset);
                layoutVersion = HashCombine(layoutVersion, item.particleCapacity);
            }
        }
        const bool compactDrawItems =
            !drawItems.empty() && (simulatedAny || !drawBuffersValid || lastCompactionLayoutVersion != layoutVersion);
        if (compactDrawItems) {
            CompactDrawItems();
            lastCompactionLayoutVersion = layoutVersion;
            drawBuffersValid = true;
        } else if (!drawItems.empty()) {
            ++stats.compactionReuseFrames;
        } else {
            drawBuffersValid = false;
        }
        // One upload slice per prepared view. Scene/PreserveColor reuse immutable shape data.
        preparedShapeAddress = 0u;
        const uint32_t shapeCount = static_cast<uint32_t>(drawItems.size());
        auto* shapes = storage.MappedShapeStates(uploadFrameSlot);
        if (shapeCount > kMaximumParticleEmitterSubmissionsPerFrame - shapeUploadCursor || (shapeCount && !shapes)) {
            ++stats.materialFailures;
            inputAccepted = false;
        } else if (shapeCount) {
            preparedShapeAddress = storage.GetShapeStateGpuAddress(uploadFrameSlot) +
                                   uint64_t{shapeUploadCursor} * sizeof(GpuParticleShapeState);
            for (const auto& item : drawItems)
                shapes[shapeUploadCursor + item.drawIndex] = BuildShapeState(item.render);
            shapeUploadCursor += shapeCount;
        }
        RefreshAllocationStats();
        stats.inputAccepted = inputAccepted;
        return inputAccepted;
    }

    bool ExecuteForwardTransparent(const GFX::Context& newContext, const ParticleGpuDrivenDrawBindings& bindings) {
        context = newContext;
        if (!initialized || drawItems.empty())
            return true;
        if (context.cmdList == nullptr || bindings.camera == 0u || bindings.materialData == 0u ||
            bindings.materialParameters == 0u || bindings.materialResources == 0u) {
            return false;
        }
        if (!preparedShapeAddress)
            return false;
        GFX::PIX::ScopedGpuEvent drawEvent(context.cmdList, GFX::PIX::kColorRender, "Particles.DrawIndirect");
        GFX::GPU_PROFILE::ScopedGpuTimer drawTimer(context.cmdList, GFX::GPU_PROFILE::Pass::ParticleDraw);
        const D3D12_GPU_DESCRIPTOR_HANDLE texturePool = GetMaterialTexturePoolSrvGpuHandle(context);
        if (texturePool.ptr == 0u)
            return false;
        const bool sceneDepthAvailable = bindings.sceneDepth.ptr != 0u;
        const auto bindParticleState = [&] {
            ID3D12DescriptorHeap* heaps[]{context.srvHeap};
            context.cmdList->SetDescriptorHeaps(1, heaps);
            context.cmdList->SetGraphicsRootSignature(pipelines.GetDrawRootSignature());
            context.cmdList->SetGraphicsRootConstantBufferView(0u, bindings.camera);
            context.cmdList->SetGraphicsRootShaderResourceView(1u, storage.GetParticleGpuAddress());
            context.cmdList->SetGraphicsRootShaderResourceView(2u, storage.GetAliveIndexGpuAddress());
            context.cmdList->SetGraphicsRootShaderResourceView(3u, bindings.materialData);
            context.cmdList->SetGraphicsRootShaderResourceView(4u, bindings.materialParameters);
            context.cmdList->SetGraphicsRootShaderResourceView(5u, bindings.materialResources);
            context.cmdList->SetGraphicsRootDescriptorTable(6u, texturePool);

            context.cmdList->SetGraphicsRootDescriptorTable(9u,
                                                            sceneDepthAvailable ? bindings.sceneDepth : texturePool);
            context.cmdList->SetGraphicsRootConstantBufferView(10u, bindings.light);
            context.cmdList->SetGraphicsRootConstantBufferView(11u, bindings.shadow);
            context.cmdList->SetGraphicsRootConstantBufferView(12u, bindings.skyEnvironment);
            context.cmdList->SetGraphicsRootShaderResourceView(13u, bindings.localLights);
            context.cmdList->SetGraphicsRootShaderResourceView(14u, bindings.localLightHeaders);
            context.cmdList->SetGraphicsRootShaderResourceView(15u, bindings.localLightIndices);
            context.cmdList->SetGraphicsRootShaderResourceView(16u, storage.GetDrawCounterGpuAddress());
            context.cmdList->SetGraphicsRootDescriptorTable(17u,
                                                            bindings.shadowMap.ptr ? bindings.shadowMap : texturePool);
            context.cmdList->SetGraphicsRootDescriptorTable(18u, bindings.irradiance.ptr ? bindings.irradiance
                                                                                         : texturePool);
            context.cmdList->SetGraphicsRootDescriptorTable(19u, bindings.prefiltered.ptr ? bindings.prefiltered
                                                                                          : texturePool);
        };
        drawOrder.clear();
        drawOrder.reserve(drawItems.size());
        for (uint32_t itemIndex = 0u; itemIndex < static_cast<uint32_t>(drawItems.size()); ++itemIndex) {
            if (bindings.postProcessMode && drawItems[itemIndex].render.postProcessMode != *bindings.postProcessMode)
                continue;
            float distanceSquared = 0.0f;
            if (bindings.cameraPositionValid) {
                const MATH::Vec3 delta = drawItems[itemIndex].worldPosition - bindings.cameraPosition;
                distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
                if (!std::isfinite(distanceSquared)) {
                    distanceSquared = 0.0f;
                }
            }
            drawOrder.push_back({itemIndex, distanceSquared});
        }
        if (bindings.cameraPositionValid) {
            std::sort(drawOrder.begin(), drawOrder.end(), [&](const DrawOrderEntry& lhs, const DrawOrderEntry& rhs) {
                const DrawItem& lhsItem = drawItems[lhs.itemIndex];
                const DrawItem& rhsItem = drawItems[rhs.itemIndex];
                const uint32_t lhsQueue = lhsItem.render.depthMode == ParticleDepthMode::ForegroundAccent ? 1u : 0u;
                const uint32_t rhsQueue = rhsItem.render.depthMode == ParticleDepthMode::ForegroundAccent ? 1u : 0u;
                if (lhsQueue != rhsQueue) {
                    return lhsQueue < rhsQueue;
                }
                if (lhsQueue == 0u && lhs.cameraDistanceSquared != rhs.cameraDistanceSquared) {
                    return lhs.cameraDistanceSquared > rhs.cameraDistanceSquared;
                }
                return lhs.itemIndex < rhs.itemIndex;
            });
        }
        const auto drawItem = [&](uint32_t itemIndex) {
            bindParticleState();
            context.cmdList->SetGraphicsRootShaderResourceView(20u, preparedShapeAddress);
            const DrawItem& item = drawItems[itemIndex];
            if (bindings.postProcessMode && item.render.postProcessMode != *bindings.postProcessMode)
                return false; // reject a malformed raw composition crossing the two routes
            const uint32_t blendIndex = item.render.blendMode == ParticleBlendMode::Additive ? 1u : 0u;
            const uint32_t depthIndex = item.render.depthMode == ParticleDepthMode::ForegroundAccent ? 0u : 1u;
            const uint32_t shapeIndex =
                item.render.shape == ParticleRenderShape::Sprite && item.render.spatialEffects.empty() ? 0u : 1u;
            ID3D12PipelineState* pipeline = pipelines.GetDrawPipeline(shapeIndex, blendIndex, depthIndex);
            if (pipeline == nullptr) {
                ++stats.materialFailures;
                return false;
            }
            context.cmdList->SetPipelineState(pipeline);
            GpuParticleDrawConstants constants{};
            constants.aliveIndexBase = item.aliveIndexBase;
            constants.reserved = item.drawIndex;
            constants.billboardMode = static_cast<uint32_t>(item.render.billboardMode);
            constants.renderShapeMode = static_cast<uint32_t>(item.render.shape);
            constants.segmentCount = ParticleGeometrySegments(item.render);
            const uint32_t flipbookGrid =
                INTERNAL::PackGpuParticleFlipbookGrid(item.render.flipbookColumns, item.render.flipbookRows);
            const uint32_t flipbookFlags =
                (item.render.flipbookLoop ? 1u : 0u) | (item.render.flipbookRandomStart ? 2u : 0u);
            const uint32_t flipbookPlayback =
                INTERNAL::PackGpuParticleFlipbookPlayback(item.render.flipbookFrameCount, flipbookFlags);
            constants.renderShapeParamsB =
                MakeGpuParticleFloat4(item.render.ringEndAngleRadians, item.render.flipbookFramesPerSecond,
                                      std::bit_cast<float>(flipbookGrid), std::bit_cast<float>(flipbookPlayback));
            constants.depthMode = static_cast<uint32_t>(item.render.depthMode);
            constants.sceneDepthAvailable = sceneDepthAvailable ? 1u : 0u;
            constants.materialFlags =
                (IsLitParticleMaterial(item.render.materialMode) && bindings.IsValid() ? 1u : 0u) |
                (item.render.effectMaterialIncludesParticleColor ? 2u : 0u);
            // z/w は形状の幅テーパー。ルートシグネチャを増やさずに運ぶ。
            constants.depthParams = MakeGpuParticleFloat4(item.render.softIntersectionDistance, item.render.depthBias,
                                                          item.render.taperStart, item.render.taperEnd);
            // プレビュー等で照明資源が無い場合、無効な CBV/SRV を参照しない。
            if (IsLitParticleMaterial(item.render.materialMode) && !bindings.IsValid())
                ++stats.lightingFallbackEmitters;
            context.cmdList->SetGraphicsRoot32BitConstants(7u, sizeof(constants) / sizeof(uint32_t), &constants, 0u);
            context.cmdList->SetGraphicsRoot32BitConstant(8u, item.materialIndex, 0u);
            if (!pipelines.UsesMeshPipeline())
                context.cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context.cmdList->ExecuteIndirect(pipelines.GetDrawCommandSignature(), 1u, storage.GetDrawArgumentResource(),
                                             static_cast<UINT64>(item.drawIndex) *
                                                     INTERNAL::kGpuParticleDrawArgumentStride +
                                                 (pipelines.UsesMeshPipeline() ? 0u : 16u),
                                             nullptr, 0u);
            ++stats.drawnEmitters;
            ++stats.indirectDispatchCommands;
            if (shapeIndex == 0u) {
                ++stats.spriteDispatchCommands;
            } else {
                ++stats.stripDispatchCommands;
            }
            return true;
        };
        // 合成所属と描画レイヤーは一度だけ索引化し、各合成から全 drawItems を走査しない。
        renderedCompositions.assign(compositions.size(), false);
        const auto instanceKey = [](ParticleEmitterInstanceKey key) {
            key.emitterIndex = UINT32_MAX;
            return key;
        };
        bool changed = compositionLookupKeys.size() != compositions.size();
        for (size_t i = 0; !changed && i < compositions.size(); ++i)
            changed = compositionLookupKeys[i] != instanceKey(compositions[i].instance);
        if (changed) {
            compositionIndices.clear();
            compositionLookupKeys.clear();
            for (size_t i = 0; i < compositions.size(); ++i) {
                const auto key = instanceKey(compositions[i].instance);
                compositionLookupKeys.push_back(key);
                compositionIndices.try_emplace(key, i);
            }
        }
        changed = itemLookupKeys.size() != drawItems.size();
        for (size_t i = 0; !changed && i < drawItems.size(); ++i)
            changed = itemLookupKeys[i] != drawItems[i].key;
        if (!compositions.empty() && changed) {
            itemIndices.clear();
            itemLookupKeys.clear();
            for (uint32_t i = 0; i < drawItems.size(); ++i) {
                itemLookupKeys.push_back(drawItems[i].key);
                itemIndices.try_emplace(drawItems[i].key, i);
            }
        }
        for (const auto& order : drawOrder) {
            const auto found = compositionIndices.find(instanceKey(drawItems[order.itemIndex].key));
            if (found == compositionIndices.end()) {
                if (!drawItem(order.itemIndex))
                    return false;
                continue;
            }
            const size_t compositionIndex = found->second;
            if (renderedCompositions[compositionIndex])
                continue;
            renderedCompositions[compositionIndex] = true;
            const auto& composition = compositions[compositionIndex];
            if (!compositionRenderer.Render(context, bindings, composition, [&](uint32_t layer) {
                    auto key = composition.instance;
                    key.emitterIndex = layer;
                    const auto item = itemIndices.find(key);
                    return item == itemIndices.end() || drawItem(item->second);
                })) {
                ++stats.materialFailures;
                return false;
            }
        }
        return true;
    }

    void Shutdown() {
        storage.Shutdown(context);
        pipelines.Shutdown();
        compositionRenderer.Release();
        compositionIndices.clear();
        itemIndices.clear();
        compositionLookupKeys.clear();
        itemLookupKeys.clear();
        renderedCompositions.clear();
        compositions.clear();
        materialResolver.Clear();
        allocations.clear();
        freeRanges.clear();
        drawItems.clear();
        drawOrder.clear();
        residentStandardTextures.clear();
        prewarmedRegistryId = 0u;
        prewarmedRegistryRevision = 0u;
        stats = {};
        emitterPageCapacity = 0u;
        emitterUploadCapacity = 0u;
        emitterUploadCursor = 0u;
        shapeUploadCursor = 0u;
        preparedShapeAddress = 0u;
        uploadFrameSerial = (std::numeric_limits<uint64_t>::max)();
        uploadFrameSlot = (std::numeric_limits<uint32_t>::max)();
        lastCompactionLayoutVersion = 0u;
        materialSyncRetryVersion = 0u;
        drawBuffersValid = false;
        initialized = false;
        initializationAttempted = false;
        context = {};
    }
};

ParticleGpuDrivenPass::ParticleGpuDrivenPass() : impl_(std::make_unique<Impl>()) {}

ParticleGpuDrivenPass::~ParticleGpuDrivenPass() { Shutdown(); }

bool ParticleGpuDrivenPass::Initialize(const GFX::Context& context, uint32_t particleCapacity,
                                       uint32_t emitterPageCapacity) {
    Shutdown();
    if (context.device == nullptr || context.srvHeap == nullptr || particleCapacity == 0u ||
        emitterPageCapacity == 0u) {
        return false;
    }
    impl_->context = context;
    impl_->stats.particleCapacity = AlignParticleCount(particleCapacity);
    impl_->emitterPageCapacity = emitterPageCapacity;
    impl_->emitterUploadCapacity = emitterPageCapacity * INTERNAL::kGpuParticleEmitterUploadPageCount;
    if (!impl_->storage.Initialize(context, impl_->stats.particleCapacity, emitterPageCapacity,
                                   kMaximumParticleEmitterSubmissionsPerFrame) ||
        !impl_->pipelines.Initialize(context.device)) {
        HIKARI_LOG_ERROR("[ParticleGpuDrivenPass][Initialize] failed.");
        impl_->Shutdown();
        return false;
    }
    impl_->freeRanges.push_back({0u, impl_->stats.particleCapacity});
    impl_->initialized = true;
    impl_->stats.ready = true;
    return true;
}

void ParticleGpuDrivenPass::Shutdown() {
    if (impl_ != nullptr)
        impl_->Shutdown();
}

bool ParticleGpuDrivenPass::PrepareFrame(const GFX::Context& context, const ParticleRenderFrameSource* source,
                                         MATERIAL::GpuMaterialRegistry& materialRegistry) {
    if (impl_ && context.device && context.srvHeap && source && source->input && source->assetRegistry) {
        const AssetRegistry& assets = *source->assetRegistry;
        if (impl_->prewarmedRegistryId != assets.GetInstanceId() ||
            impl_->prewarmedRegistryRevision != assets.GetRevision()) {
            // 空の初回フレームでも、登録済み VFX の描画資源を準備する。事件の再生・時刻・乱数は進めない。
            // 実際に参照する貼図だけを同じ常駐経路へ渡し、内蔵カタログ全体の遅延読込を避ける。
            ParticleRenderInput dependencies{};
            for (const AssetDescriptor* base : assets.CollectByType(AssetType::VfxEffect)) {
                const auto* descriptor = dynamic_cast<const VfxAssetDescriptor*>(base);
                if (descriptor == nullptr || !descriptor->program) {
                    continue;
                }
                for (const VFX::VfxProgramEmitter& emitter : descriptor->program->emitters) {
                    if ((emitter.kind != VFX::VfxLayerKind::Particles && emitter.kind != VFX::VfxLayerKind::Shape) ||
                        !emitter.render.meshAssetGuid.empty()) {
                        continue;
                    }
                    dependencies.emitters.emplace_back().render = emitter.render;
                }
            }
            if (!dependencies.emitters.empty()) {
                if (!impl_->initialized && !impl_->initializationAttempted) {
                    (void)Initialize(context);
                    impl_->initializationAttempted = true;
                }
                if (impl_->initialized) {
                    // 不足資産があっても他の描画は止めず、実際の再生時に既存の検証経路で再試行する。
                    (void)impl_->EnsureStandardTextureResidency(*source, dependencies);
                    // 標準材質の中立貼図やカスタム材質の投影も初回再生から移す。粒子の生成要求は発行しない。
                    for (const auto& dependency : dependencies.emitters) {
                        (void)impl_->materialResolver.Resolve(dependency.render, &assets, source->externalValues);
                    }
                }
            }
            impl_->prewarmedRegistryId = assets.GetInstanceId();
            impl_->prewarmedRegistryRevision = assets.GetRevision();
        }
    }
    // 登録アセットを持たないツールの直接再生は、従来の初期化経路を維持する。
    if (impl_ && !impl_->initialized && !impl_->initializationAttempted && context.device && context.srvHeap &&
        source && source->input && !source->input->emitters.empty()) {
        (void)Initialize(context);
        impl_->initializationAttempted = true;
    }
    return impl_ != nullptr && impl_->PrepareFrame(context, source, materialRegistry);
}

bool ParticleGpuDrivenPass::ExecuteForwardTransparent(const GFX::Context& context,
                                                      const ParticleGpuDrivenDrawBindings& bindings) {
    return impl_ != nullptr && impl_->ExecuteForwardTransparent(context, bindings);
}

void ParticleGpuDrivenPass::ReleaseNamespace(uint64_t playbackNamespace) {
    if (impl_ != nullptr)
        impl_->ReleaseNamespace(playbackNamespace);
}

bool ParticleGpuDrivenPass::HasDrawWork(std::optional<VFX::VfxPostProcessMode> mode) const noexcept {
    return impl_ && std::any_of(impl_->drawItems.begin(), impl_->drawItems.end(),
                                [&](const auto& item) { return !mode || item.render.postProcessMode == *mode; });
}

const ParticleGpuDrivenPassStats& ParticleGpuDrivenPass::GetStats() const noexcept {
    static const ParticleGpuDrivenPassStats empty{};
    return impl_ != nullptr ? impl_->stats : empty;
}

} // namespace HIKARI::RENDER3D::PARTICLES
