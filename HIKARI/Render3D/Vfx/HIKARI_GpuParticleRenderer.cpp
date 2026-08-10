#include "Render3D/Vfx/HIKARI_GpuParticleRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/HIKARI_TimeService.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Vfx/HIKARI_GpuParticleParameterPacking.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleMaterialSet.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticlePipelineState.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleShaderInterop.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleStorage.h"

namespace HIKARI::RENDER3D::PARTICLES {
    namespace {
        using Float4 = INTERNAL::GpuParticleFloat4;
        using GpuEmitterParameters =
            INTERNAL::GpuParticleEmitterParameters;
        using SimulationConstants =
            INTERNAL::GpuParticleSimulationConstants;
        using DrawConstants = INTERNAL::GpuParticleDrawConstants;
        using ResolvedMaterial = INTERNAL::ResolvedParticleMaterial;
        using INTERNAL::BuildGpuParticleEmitterParameters;
        using INTERNAL::MakeGpuParticleFloat4;

        inline constexpr uint32_t kThreadGroupSize = 64u;

        struct ParticleEmitterInstanceKeyHasher {
            size_t operator()(
                const ParticleEmitterInstanceKey& key) const noexcept {

                size_t value = static_cast<size_t>(
                    key.playbackNamespace);
                value ^= static_cast<size_t>(key.playbackSlot) +
                    0x9e3779b9u + (value << 6u) + (value >> 2u);
                value ^= static_cast<size_t>(key.playbackGeneration) +
                    0x9e3779b9u + (value << 6u) + (value >> 2u);
                value ^= static_cast<size_t>(key.emitterIndex) +
                    0x9e3779b9u + (value << 6u) + (value >> 2u);
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
            uint64_t lastSimulatedFrame =
                (std::numeric_limits<uint64_t>::max)();
            uint32_t highestSimulationOrdinal = 0u;
            bool simulatedThisFrame = false;
        };

        uint32_t AlignParticleCount(uint32_t value) noexcept {
            if (value == 0u) {
                return 0u;
            }
            return (value + kThreadGroupSize - 1u) /
                kThreadGroupSize * kThreadGroupSize;
        }

        uint32_t DispatchGroupCount(uint32_t value) noexcept {
            return (value + kThreadGroupSize - 1u) /
                kThreadGroupSize;
        }

        Float4 MakeFloat4(
            float x,
            float y,
            float z,
            float w) noexcept {
            return MakeGpuParticleFloat4(x, y, z, w);
        }

    } // namespace

    struct GpuParticleRenderer::Impl {
        GFX::Context context{};
        INTERNAL::GpuParticleStorage storage{};
        INTERNAL::GpuParticlePipelineState pipelines{};
        INTERNAL::GpuParticleMaterialSet materials{};
        std::unordered_map<
            ParticleEmitterInstanceKey,
            EmitterAllocation,
            ParticleEmitterInstanceKeyHasher> allocations{};
        std::vector<FreeRange> freeRanges{};
        GpuParticleRendererStats stats{};
        uint32_t emitterPageCapacity = 0u;
        uint32_t emitterUploadCapacity = 0u;
        uint32_t emitterUploadCursor = 0u;
        uint32_t uploadFrameSlot =
            (std::numeric_limits<uint32_t>::max)();
        bool initialized = false;

        void CoalesceFreeRanges() {
            std::sort(
                freeRanges.begin(),
                freeRanges.end(),
                [](const FreeRange& lhs, const FreeRange& rhs) {
                    return lhs.offset < rhs.offset;
                });
            std::vector<FreeRange> merged{};
            merged.reserve(freeRanges.size());
            for (const FreeRange& range : freeRanges) {
                if (range.count == 0u) {
                    continue;
                }
                if (!merged.empty() &&
                    merged.back().offset + merged.back().count ==
                        range.offset) {
                    merged.back().count += range.count;
                } else {
                    merged.push_back(range);
                }
            }
            freeRanges = std::move(merged);
        }

        void ReleaseAllocation(
            const ParticleEmitterInstanceKey& key) {
            const auto found = allocations.find(key);
            if (found == allocations.end()) {
                return;
            }
            freeRanges.push_back({
                found->second.offset,
                found->second.reservedCapacity
            });
            allocations.erase(found);
            CoalesceFreeRanges();
        }

        void ReleaseNamespace(uint64_t playbackNamespace) {
            if (playbackNamespace == 0u) {
                return;
            }
            bool released = false;
            for (auto allocation = allocations.begin();
                 allocation != allocations.end();) {
                if (allocation->first.playbackNamespace !=
                    playbackNamespace) {
                    ++allocation;
                    continue;
                }
                freeRanges.push_back({
                    allocation->second.offset,
                    allocation->second.reservedCapacity
                });
                allocation = allocations.erase(allocation);
                released = true;
            }
            if (released) {
                CoalesceFreeRanges();
            }
            stats.activeEmitters = static_cast<uint32_t>(
                allocations.size());
            stats.allocatedParticles = 0u;
            for (const auto& [key, allocation] : allocations) {
                (void)key;
                stats.allocatedParticles += allocation.reservedCapacity;
            }
        }

        EmitterAllocation* AcquireAllocation(
            const ParticleEmitterInstanceKey& key,
            uint32_t requestedCapacity,
            bool& outReset) {

            outReset = false;
            const uint32_t alignedCapacity = AlignParticleCount(
                requestedCapacity);
            if (alignedCapacity == 0u ||
                alignedCapacity > stats.particleCapacity) {
                ++stats.allocationFailures;
                return nullptr;
            }

            auto existing = allocations.find(key);
            if (existing != allocations.end() &&
                existing->second.reservedCapacity >= alignedCapacity) {
                if (existing->second.activeCapacity !=
                    requestedCapacity) {
                    existing->second.activeCapacity = requestedCapacity;
                    existing->second.spawnCursor = 0u;
                    existing->second.resetPending = true;
                    outReset = true;
                }
                return &existing->second;
            }
            if (existing != allocations.end()) {
                ReleaseAllocation(key);
                outReset = true;
            }

            for (size_t index = 0u;
                 index < freeRanges.size();
                 ++index) {
                FreeRange& range = freeRanges[index];
                if (range.count < alignedCapacity) {
                    continue;
                }
                EmitterAllocation allocation{};
                allocation.offset = range.offset;
                allocation.reservedCapacity = alignedCapacity;
                allocation.activeCapacity = requestedCapacity;
                range.offset += alignedCapacity;
                range.count -= alignedCapacity;
                if (range.count == 0u) {
                    freeRanges.erase(
                        freeRanges.begin() +
                        static_cast<std::ptrdiff_t>(index));
                }
                auto [inserted, success] = allocations.emplace(
                    key,
                    allocation);
                (void)success;
                outReset = true;
                return &inserted->second;
            }
            ++stats.allocationFailures;
            return nullptr;
        }

        void Dispatch(
            uint32_t pipelineIndex,
            const SimulationConstants& constants,
            uint32_t threadCount,
            D3D12_GPU_VIRTUAL_ADDRESS emitterUploadAddress) {

            ID3D12PipelineState* pipeline =
                pipelines.GetComputePipeline(pipelineIndex);
            if (threadCount == 0u || pipeline == nullptr ||
                emitterUploadAddress == 0u) {
                return;
            }
            ID3D12GraphicsCommandList* commandList =
                context.cmdList;
            commandList->SetComputeRootSignature(
                pipelines.GetComputeRootSignature());
            commandList->SetPipelineState(pipeline);
            commandList->SetComputeRootDescriptorTable(
                0u,
                storage.GetParticleUavGpuHandle());
            commandList->SetComputeRootShaderResourceView(
                1u,
                emitterUploadAddress);
            commandList->SetComputeRoot32BitConstants(
                2u,
                sizeof(constants) / sizeof(uint32_t),
                &constants,
                0u);
            commandList->Dispatch(
                DispatchGroupCount(threadCount),
                1u,
                1u);
        }

        bool SimulateEmitter(
            const ParticleEmitterSubmission& submission,
            EmitterAllocation& allocation,
            bool reset,
            uint32_t emitterUploadIndex,
            uint32_t uploadIndex) {

            const INTERNAL::GpuParticleEmitterUploadAddress uploadAddress =
                INTERNAL::ResolveGpuParticleEmitterUploadAddress(
                    emitterUploadIndex,
                    emitterPageCapacity);
            if (emitterUploadIndex >= emitterUploadCursor ||
                emitterUploadIndex >= emitterUploadCapacity ||
                !uploadAddress.valid ||
                uploadIndex >= GFX::kFrameResourceCount) {
                return false;
            }
            const D3D12_GPU_VIRTUAL_ADDRESS emitterUploadAddress =
                storage.GetEmitterUploadGpuAddress(
                    uploadIndex,
                    uploadAddress.pageIndex);
            if (emitterUploadAddress == 0u) {
                return false;
            }

            SimulationConstants constants{};
            constants.particleOffset = allocation.offset;
            constants.particleCapacity = allocation.activeCapacity;
            constants.emitterIndex = uploadAddress.elementIndex;
            constants.spawnCursor = allocation.spawnCursor;
            constants.spawnCount = (std::min)(
                submission.spawnCount,
                allocation.activeCapacity);
            constants.randomSeed = submission.randomSeed;
            constants.deltaSeconds = (std::max)(
                submission.deltaSeconds,
                0.0f);

            if (reset || submission.resetSimulation) {
                Dispatch(
                    0u,
                    constants,
                    allocation.activeCapacity,
                    emitterUploadAddress);
                storage.InsertUavBarrier(context);
            } else if (constants.deltaSeconds > 0.0f) {
                Dispatch(
                    1u,
                    constants,
                    allocation.activeCapacity,
                    emitterUploadAddress);
                storage.InsertUavBarrier(context);
            }
            if (constants.spawnCount > 0u) {
                Dispatch(
                    2u,
                    constants,
                    constants.spawnCount,
                    emitterUploadAddress);
                storage.InsertUavBarrier(context);
                allocation.spawnCursor =
                    (allocation.spawnCursor + constants.spawnCount) %
                    allocation.activeCapacity;
                stats.spawnedParticles += constants.spawnCount;
            }
            ++stats.simulatedEmitters;
            return true;
        }

        bool Render(
            const ParticleRenderInput& input,
            const Camera3D& camera,
            const AssetRegistry* assetRegistry,
            const MATERIAL::MaterialExternalValueSnapshot*
                externalValues,
            ParticleRenderViewExtent viewExtent) {

            if (!initialized ||
                context.cmdList == nullptr ||
                context.srvHeap == nullptr ||
                !storage.IsReady() ||
                !viewExtent.IsValid()) {
                return false;
            }

            if (uploadFrameSlot != context.frameIndex) {
                uploadFrameSlot = context.frameIndex;
                emitterUploadCursor = 0u;
                materials.BeginFrame();
                stats.simulatedEmitters = 0u;
                stats.drawnEmitters = 0u;
                stats.spawnedParticles = 0u;
                stats.simulationFailures = 0u;
                stats.emitterUploadsUsed = 0u;
                stats.emitterUploadBudgetFailures = 0u;
                stats.materialUploadFailures = 0u;
            }
            for (const ParticleEmitterInstanceKey& key :
                 input.retiredEmitters) {
                ReleaseAllocation(key);
            }

            ID3D12DescriptorHeap* heaps[] = { context.srvHeap };
            context.cmdList->SetDescriptorHeaps(1u, heaps);
            storage.Transition(
                context,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const uint32_t uploadIndex =
                context.frameIndex % GFX::kFrameResourceCount;

            struct DrawItem {
                const ParticleEmitterSubmission* submission = nullptr;
                EmitterAllocation* allocation = nullptr;
                ResolvedMaterial material{};
            };
            std::vector<DrawItem> drawItems{};
            drawItems.reserve(input.emitters.size());
            std::unordered_map<
                ParticleEmitterInstanceKey,
                size_t,
                ParticleEmitterInstanceKeyHasher> drawItemIndices{};
            struct StagedEmitterParameters {
                uint32_t uploadIndex = UINT32_MAX;
                GpuEmitterParameters parameters{};
            };
            struct SimulationProgress {
                uint32_t highestOrdinal = 0u;
                bool simulated = false;
            };
            std::unordered_map<
                ParticleEmitterInstanceKey,
                SimulationProgress,
                ParticleEmitterInstanceKeyHasher> simulationProgress{};
            std::unordered_map<
                ParticleEmitterInstanceKey,
                GpuEmitterParameters,
                ParticleEmitterInstanceKeyHasher> preflightParameters{};
            uint32_t requiredEmitterUploads = 0u;
            for (const ParticleEmitterSubmission& submission :
                 input.emitters) {
                auto [progressIt, inserted] =
                    simulationProgress.try_emplace(submission.key);
                SimulationProgress& progress = progressIt->second;
                if (inserted) {
                    const auto allocation = allocations.find(
                        submission.key);
                    if (allocation != allocations.end() &&
                        allocation->second.lastSimulatedFrame ==
                            input.frameIndex) {
                        progress.highestOrdinal =
                            allocation->second.highestSimulationOrdinal;
                        progress.simulated =
                            allocation->second.simulatedThisFrame;
                    }
                }
                const bool needsSimulation = !progress.simulated ||
                    submission.simulationOrdinal >
                        progress.highestOrdinal;
                if (!needsSimulation) continue;

                const GpuEmitterParameters parameters =
                    BuildGpuParticleEmitterParameters(submission);
                const auto previous = preflightParameters.find(
                    submission.key);
                if (previous == preflightParameters.end() ||
                    std::memcmp(
                        &previous->second,
                        &parameters,
                        sizeof(parameters)) != 0) {
                    preflightParameters[submission.key] = parameters;
                    ++requiredEmitterUploads;
                }
                progress.simulated = true;
                progress.highestOrdinal = submission.simulationOrdinal;
            }
            const uint32_t remainingEmitterUploads =
                emitterUploadCursor <= emitterUploadCapacity
                ? emitterUploadCapacity - emitterUploadCursor
                : 0u;
            if (requiredEmitterUploads > remainingEmitterUploads) {
                ++stats.emitterUploadBudgetFailures;
                stats.simulationFailures += requiredEmitterUploads;
                stats.emitterUploadsUsed = emitterUploadCursor;
                return false;
            }

            std::unordered_map<
                ParticleEmitterInstanceKey,
                StagedEmitterParameters,
                ParticleEmitterInstanceKeyHasher> stagedEmitters{};
            const auto stageEmitter = [&](
                const ParticleEmitterSubmission& submission) -> uint32_t {
                const GpuEmitterParameters parameters =
                    BuildGpuParticleEmitterParameters(submission);
                const auto found = stagedEmitters.find(submission.key);
                if (found != stagedEmitters.end() &&
                    std::memcmp(
                        &found->second.parameters,
                        &parameters,
                        sizeof(parameters)) == 0) {
                    return found->second.uploadIndex;
                }
                if (emitterUploadCursor >= emitterUploadCapacity) {
                    return UINT32_MAX;
                }
                const uint32_t index = emitterUploadCursor;
                const INTERNAL::GpuParticleEmitterUploadAddress address =
                    INTERNAL::ResolveGpuParticleEmitterUploadAddress(
                        index,
                        emitterPageCapacity);
                if (!storage.WriteEmitterParameters(
                        context.device,
                        uploadIndex,
                        address,
                        parameters)) {
                    return UINT32_MAX;
                }
                ++emitterUploadCursor;
                stagedEmitters[submission.key] = {index, parameters};
                stats.emitterUploadsUsed = emitterUploadCursor;
                return index;
            };
            for (const ParticleEmitterSubmission& submission :
                 input.emitters) {
                bool reset = false;
                EmitterAllocation* allocation = AcquireAllocation(
                    submission.key,
                    submission.simulation.capacity,
                    reset);
                if (allocation == nullptr) {
                    continue;
                }
                if (allocation->lastSimulatedFrame != input.frameIndex) {
                    allocation->lastSimulatedFrame = input.frameIndex;
                    allocation->highestSimulationOrdinal = 0u;
                    allocation->simulatedThisFrame = false;
                }
                const bool needsSimulation =
                    !allocation->simulatedThisFrame ||
                    submission.simulationOrdinal >
                        allocation->highestSimulationOrdinal;
                if (needsSimulation) {
                    const uint32_t emitterIndex = stageEmitter(submission);
                    if (emitterIndex == UINT32_MAX) {
                        ++stats.simulationFailures;
                        continue;
                    }
                    if (!SimulateEmitter(
                            submission,
                            *allocation,
                            reset || allocation->resetPending,
                            emitterIndex,
                            uploadIndex)) {
                        ++stats.simulationFailures;
                        continue;
                    }
                    allocation->simulatedThisFrame = true;
                    allocation->highestSimulationOrdinal =
                        submission.simulationOrdinal;
                    allocation->resetPending = false;
                }
                if (!submission.drawAfterSimulation) {
                    continue;
                }
                DrawItem item{
                    &submission,
                    allocation,
                    materials.Resolve(
                        submission.render.materialAssetGuid,
                        assetRegistry,
                        externalValues)
                };
                const auto existing = drawItemIndices.find(submission.key);
                if (existing == drawItemIndices.end()) {
                    drawItemIndices.emplace(
                        submission.key, drawItems.size());
                    drawItems.push_back(std::move(item));
                } else {
                    drawItems[existing->second] = std::move(item);
                }
            }

            storage.Transition(
                context,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.cmdList->SetGraphicsRootSignature(
                pipelines.GetDrawRootSignature());
            context.cmdList->SetGraphicsRootDescriptorTable(
                0u,
                storage.GetParticleSrvGpuHandle());
            context.cmdList->SetGraphicsRootShaderResourceView(
                1u,
                materials.GetMaterialGpuAddress(uploadIndex));
            context.cmdList->SetGraphicsRootShaderResourceView(
                2u,
                materials.GetParameterGpuAddress(uploadIndex));
            context.cmdList->SetGraphicsRootShaderResourceView(
                3u,
                materials.GetResourceGpuAddress(uploadIndex));
            const D3D12_GPU_DESCRIPTOR_HANDLE texturePool =
                GetMaterialTexturePoolSrvGpuHandle(context);
            if (texturePool.ptr == 0u) {
                return false;
            }
            context.cmdList->SetGraphicsRootDescriptorTable(
                4u,
                texturePool);
            context.cmdList->IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            const MATH::Mat4 cameraWorld = MATH::Inverse(
                camera.GetView());
            DrawConstants drawConstants{};
            drawConstants.viewProjection = camera.GetViewProj();
            drawConstants.cameraRight = MakeFloat4(
                cameraWorld.m[0][0],
                cameraWorld.m[0][1],
                cameraWorld.m[0][2],
                0.0f);
            drawConstants.cameraUp = MakeFloat4(
                cameraWorld.m[1][0],
                cameraWorld.m[1][1],
                cameraWorld.m[1][2],
                0.0f);
            const MATH::Vec3 cameraPosition = camera.GetPosition();
            drawConstants.cameraPosition = MakeFloat4(
                cameraPosition.x,
                cameraPosition.y,
                cameraPosition.z,
                1.0f);
            const float screenWidth = static_cast<float>(
                viewExtent.width);
            const float screenHeight = static_cast<float>(
                viewExtent.height);
            drawConstants.screenParams = MakeFloat4(
                screenWidth,
                screenHeight,
                1.0f / screenWidth,
                1.0f / screenHeight);
            const FrameContext& frame = TIME::GetFrameContext();
            drawConstants.timeParams = MakeFloat4(
                TIME::GetUnscaledElapsedSeconds(),
                frame.unscaledDt,
                frame.gameDt,
                static_cast<float>(frame.frameIndex));
            for (const DrawItem& item : drawItems) {
                if (item.submission == nullptr ||
                    item.allocation == nullptr ||
                    item.allocation->activeCapacity == 0u) {
                    continue;
                }
                const ParticleEmitterRenderParameters& render =
                    item.submission->render;
                const uint32_t blendIndex =
                    render.blendMode == ParticleBlendMode::Additive
                        ? 1u
                        : 0u;
                const uint32_t depthIndex = render.depthTest ? 1u : 0u;
                context.cmdList->SetPipelineState(
                    pipelines.GetDrawPipeline(blendIndex, depthIndex));
                uint32_t materialIndex = 0u;
                if (!materials.Upload(
                        item.material,
                        uploadIndex,
                        materialIndex)) {
                    ++stats.materialUploadFailures;
                    continue;
                }
                drawConstants.billboardMode = static_cast<uint32_t>(
                    render.billboardMode);
                drawConstants.renderShapeMode =
                    static_cast<uint32_t>(render.shape);
                drawConstants.renderShapeParam0 =
                    render.ribbonSegmentCount;
                drawConstants.renderShapeParam1 =
                    render.ringSegmentCount;
                drawConstants.renderShapeParamsA = MakeFloat4(
                    render.ribbonWidth,
                    render.ringOuterRadius,
                    render.ringInnerRadius,
                    render.ringStartAngleRadians);
                drawConstants.renderShapeParamsB = MakeFloat4(
                    render.ringEndAngleRadians,
                    0.0f,
                    0.0f,
                    0.0f);
                drawConstants.flipbookColumns =
                    render.flipbookColumns;
                drawConstants.flipbookRows =
                    render.flipbookRows;
                drawConstants.flipbookFrameCount =
                    render.flipbookFrameCount;
                drawConstants.flipbookFlags =
                    (render.flipbookLoop ? 1u : 0u) |
                    (render.flipbookRandomStart ? 2u : 0u);
                drawConstants.flipbookParams = MakeFloat4(
                    render.flipbookFramesPerSecond,
                    0.0f,
                    0.0f,
                    0.0f);
                context.cmdList->SetGraphicsRoot32BitConstants(
                    5u,
                    sizeof(drawConstants) / sizeof(uint32_t),
                    &drawConstants,
                    0u);
                const std::array<uint32_t, 4u> materialConstants{
                    materialIndex, 0u, 0u, 0u
                };
                context.cmdList->SetGraphicsRoot32BitConstants(
                    6u,
                    static_cast<UINT>(materialConstants.size()),
                    materialConstants.data(),
                    0u);
                uint32_t verticesPerInstance = 6u;
                if (render.shape == ParticleRenderShape::Ribbon) {
                    verticesPerInstance =
                        (std::max)(2u, render.ribbonSegmentCount) * 6u;
                } else if (render.shape == ParticleRenderShape::Ring) {
                    verticesPerInstance =
                        (std::max)(3u, render.ringSegmentCount) * 6u;
                }
                context.cmdList->DrawInstanced(
                    verticesPerInstance,
                    item.allocation->activeCapacity,
                    0u,
                    item.allocation->offset);
                ++stats.drawnEmitters;
            }

            stats.activeEmitters = static_cast<uint32_t>(
                allocations.size());
            stats.allocatedParticles = 0u;
            for (const auto& [key, allocation] : allocations) {
                (void)key;
                stats.allocatedParticles +=
                    allocation.reservedCapacity;
            }
            return true;
        }

        void Shutdown() {
            storage.Shutdown(context);
            materials.Shutdown();
            pipelines.Shutdown();
            allocations.clear();
            freeRanges.clear();
            stats = {};
            emitterPageCapacity = 0u;
            emitterUploadCapacity = 0u;
            emitterUploadCursor = 0u;
            uploadFrameSlot =
                (std::numeric_limits<uint32_t>::max)();
            initialized = false;
            context = {};
        }
    };

    GpuParticleRenderer::GpuParticleRenderer()
        : impl_(std::make_unique<Impl>()) {}

    GpuParticleRenderer::~GpuParticleRenderer() {
        Shutdown();
    }

    bool GpuParticleRenderer::Initialize(
        const GFX::Context& context,
        uint32_t particleCapacity,
        uint32_t emitterCapacity) {

        Shutdown();
        if (context.device == nullptr ||
            context.srvHeap == nullptr ||
            particleCapacity == 0u ||
            emitterCapacity <
                kDefaultParticleEmitterUploadPageCapacity ||
            emitterCapacity >
                (std::numeric_limits<uint32_t>::max)() /
                    INTERNAL::kGpuParticleEmitterUploadPageCount) {
            return false;
        }
        impl_->context = context;
        impl_->stats.particleCapacity = AlignParticleCount(
            particleCapacity);
        impl_->emitterPageCapacity = emitterCapacity;
        impl_->emitterUploadCapacity = emitterCapacity *
            INTERNAL::kGpuParticleEmitterUploadPageCount;
        impl_->stats.emitterUploadCapacity =
            impl_->emitterUploadCapacity;
        if (!impl_->storage.Initialize(
                impl_->context,
                impl_->stats.particleCapacity,
                impl_->emitterPageCapacity) ||
            !impl_->materials.Initialize(
                impl_->context.device,
                impl_->emitterUploadCapacity) ||
            !impl_->pipelines.Initialize(impl_->context.device)) {
            impl_->Shutdown();
            return false;
        }
        impl_->freeRanges.push_back({
            0u,
            impl_->stats.particleCapacity
        });
        impl_->initialized = true;
        impl_->stats.ready = true;
        return true;
    }

    void GpuParticleRenderer::UpdateContext(
        const GFX::Context& context) noexcept {
        if (impl_ != nullptr) {
            impl_->context = context;
        }
    }

    void GpuParticleRenderer::Shutdown() {
        if (impl_ != nullptr) {
            impl_->Shutdown();
        }
    }

    bool GpuParticleRenderer::Render(
        const ParticleRenderInput& input,
        const Camera3D& camera,
        const AssetRegistry* assetRegistry,
        const MATERIAL::MaterialExternalValueSnapshot*
            externalValues,
        ParticleRenderViewExtent viewExtent) {
        return impl_ != nullptr &&
            impl_->Render(
                input,
                camera,
                assetRegistry,
                externalValues,
                viewExtent);
    }

    void GpuParticleRenderer::ReleaseNamespace(
        uint64_t playbackNamespace) {
        if (impl_ != nullptr) {
            impl_->ReleaseNamespace(playbackNamespace);
        }
    }

    const GpuParticleRendererStats&
        GpuParticleRenderer::GetStats() const noexcept {
        static const GpuParticleRendererStats empty{};
        return impl_ != nullptr ? impl_->stats : empty;
    }

} // namespace HIKARI::RENDER3D::PARTICLES
