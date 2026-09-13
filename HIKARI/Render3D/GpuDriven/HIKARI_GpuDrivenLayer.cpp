#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"

#include <algorithm>
#include <cstring>

#include "Core/HIKARI_Logger.h"
#include "Core/Numeric/HIKARI_IntegerConversion.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuScene.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        bool HasTraditionalCommandStreamWork(
            const GpuDrivenSceneSource* source) {

            if (source == nullptr) {
                return false;
            }

            for (const GpuDrivenPassSource& pass : source->passes) {
                if (pass.traditionalIndirect.HasCommands()) {
                    return true;
                }
            }
            return false;
        }

        GpuDrivenPassKind PassFromIndex(size_t index) {
            return static_cast<GpuDrivenPassKind>(
                (std::min)(index, kGpuDrivenPassCount - 1u));
        }

        GpuDrivenPassKind ResolveExecutionSourcePass(
            GpuDrivenPassKind pass) {

            return pass == GpuDrivenPassKind::GeometryAux
                ? GpuDrivenPassKind::ForwardOpaque
                : pass;
        }

        bool UsesGeometryAuxPipeline(GpuDrivenPassKind pass) {
            return pass == GpuDrivenPassKind::GeometryAux;
        }

        bool HasCommandBucketCapacity(
            const GpuCommandBuildResult& commands,
            GpuDrivenPassKind pass) {

            return commands.layout.GetPass(pass).commandBucketCapacity != 0;
        }

        size_t CountPrimaryInstances(const GpuDrivenPassSource& pass) {
            return pass.gpuSceneInstanceCount != 0u
                ? pass.gpuSceneInstanceCount
                : (pass.instances != nullptr ? pass.instances->size() : 0u);
        }

        size_t CountPassInstances(const GpuDrivenPassSource& pass) {
            return
                CountPrimaryInstances(pass) +
                static_cast<size_t>(pass.traditionalIndirect.gpuSceneInstanceCount);
        }

        uint32_t ResolvePassBaseIndex(const GpuDrivenPassSource& pass) {
            return pass.HasPrimaryGpuSceneRange()
                ? pass.gpuSceneBaseIndex
                : 0u;
        }

        // Seed / material patch / meshlet cull は registry が割り当てた
        // gpuSceneBaseIndex を絶対 index として参照するため、upload も
        // append ではなく base index へ位置指定で書き込む必要がある。
        void UploadSceneInstancesAt(
            SurfaceGpuSceneFrameBuffer& buffer,
            uint32_t gpuSceneBaseIndex,
            const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances) {

            if (instances == nullptr || instances->empty()) {
                return;
            }
            buffer.UpdateRange(
                gpuSceneBaseIndex,
                instances->data(),
                instances->size());
        }

        bool SceneSourceLayoutTilesInstanceBuffer(
            const GpuDrivenSceneSource& source,
            size_t sourceInstanceCount) {

            size_t maxRegionEnd = 0;
            for (const GpuDrivenPassSource& pass : source.passes) {
                const size_t primaryCount = CountPrimaryInstances(pass);
                if (primaryCount != 0u) {
                    maxRegionEnd = (std::max)(
                        maxRegionEnd,
                        static_cast<size_t>(pass.gpuSceneBaseIndex) +
                            primaryCount);
                }
                const size_t traditionalCount =
                    pass.traditionalIndirect.gpuSceneInstanceCount;
                if (traditionalCount != 0u) {
                    maxRegionEnd = (std::max)(
                        maxRegionEnd,
                        static_cast<size_t>(
                            pass.traditionalIndirect.gpuSceneBaseIndex) +
                            traditionalCount);
                }
            }
            return maxRegionEnd <= sourceInstanceCount;
        }

        bool PatchDirtyPass(
            SurfaceGpuSceneFrameBuffer& buffer,
            const GpuDrivenPassSource& pass,
            std::vector<GpuSceneDirtyRange>& ranges) {

            std::erase_if(ranges, [](const GpuSceneDirtyRange& range) {
                return !range.IsValid();
            });
            if (ranges.empty()) {
                return true;
            }
            if (pass.instances == nullptr) {
                return false;
            }
            for (const GpuSceneDirtyRange& range : ranges) {
                const size_t localBegin = range.firstInstance;
                const size_t localCount = range.instanceCount;
                if (localBegin >= pass.instances->size() ||
                    localCount > pass.instances->size() - localBegin) {
                    return false;
                }
            }
            std::sort(ranges.begin(), ranges.end(),
                [](const GpuSceneDirtyRange& left, const GpuSceneDirtyRange& right) {
                    return left.firstInstance < right.firstInstance;
                });
            for (size_t index = 0; index < ranges.size(); ++index) {
                const size_t begin = ranges[index].firstInstance;
                size_t end = begin + ranges[index].instanceCount;
                while (index + 1 < ranges.size() && ranges[index + 1].firstInstance <= end) {
                    ++index;
                    end = (std::max)(end, static_cast<size_t>(ranges[index].firstInstance) +
                        ranges[index].instanceCount);
                }
                if (!buffer.UpdateRange(
                    static_cast<size_t>(pass.gpuSceneBaseIndex) + begin,
                    pass.instances->data() + begin,
                    end - begin)) {
                    return false;
                }
            }
            return true;
        }

        void UploadFullScene(
            SurfaceGpuSceneFrameBuffer& buffer,
            const GpuDrivenSceneSource& source) {

            buffer.ResetFrame();
            const std::vector<RUNTIME::SurfaceGpuSceneInstance>*
                uploadedPrimaryInstances = nullptr;
            uint32_t uploadedPrimaryBaseIndex = 0;
            uint32_t uploadedPrimaryCount = 0;
            for (size_t passIndex = 0u;
                passIndex < kGpuDrivenPassCount;
                ++passIndex) {

                const GpuDrivenPassSource& pass = source.passes[passIndex];
                const bool primaryAlreadyUploaded =
                    pass.instances != nullptr &&
                    pass.instances == uploadedPrimaryInstances &&
                    pass.gpuSceneBaseIndex == uploadedPrimaryBaseIndex &&
                    pass.gpuSceneInstanceCount == uploadedPrimaryCount;
                if (!primaryAlreadyUploaded) {
                    UploadSceneInstancesAt(
                        buffer,
                        pass.gpuSceneBaseIndex,
                        pass.instances);
                    if (pass.instances != nullptr) {
                        uploadedPrimaryInstances = pass.instances;
                        uploadedPrimaryBaseIndex = pass.gpuSceneBaseIndex;
                        uploadedPrimaryCount = pass.gpuSceneInstanceCount;
                    }
                }
                UploadSceneInstancesAt(
                    buffer,
                    pass.traditionalIndirect.gpuSceneBaseIndex,
                    pass.traditionalIndirect.instances);
            }
        }

        void UploadTraditionalIndirectCommands(
            GpuTraditionalCommandStreamBuffer& buffer,
            const GpuDrivenSceneSource& source) {

            for (const GpuDrivenPassSource& pass : source.passes) {
                const GpuDrivenTraditionalIndirectView& view =
                    pass.traditionalIndirect;
                if (view.commands == nullptr || view.commands->empty()) {
                    continue;
                }
                buffer.UploadCommandSeeds(view);
            }
        }
    }

    void GpuDrivenSceneResidency::Reset() {
        resident = false;
        layoutVersion = 0;
        sourceVersion = 0;
        instanceCount = 0;
    }

    void GpuDrivenLayer::TrackSceneUploadDelta(
        const GpuDrivenSceneSource& source,
        uintptr_t sourceIdentity,
        size_t instanceCount) {

        auto& history = sceneUploadHistory_;
        if (history.sourceIdentity != sourceIdentity ||
            history.layoutVersion != source.layoutVersion ||
            history.instanceCount != instanceCount) {
            history = {};
            history.sourceIdentity = sourceIdentity;
            history.layoutVersion = source.layoutVersion;
            history.instanceCount = instanceCount;
        }
        if (history.sourceVersion == source.sourceVersion) {
            return;
        }
        if (history.sourceVersion != source.dirtyBaseSourceVersion ||
            !source.HasAnyDirtyGpuSceneRanges()) {
            history.deltas.clear();
        }
        history.sourceVersion = source.sourceVersion;
        if (!source.HasAnyDirtyGpuSceneRanges()) {
            return;
        }
        SceneUploadDelta delta{};
        delta.baseVersion = source.dirtyBaseSourceVersion;
        delta.sourceVersion = source.sourceVersion;
        for (size_t passIndex = 0; passIndex < kGpuDrivenPassCount; ++passIndex) {
            delta.ranges[passIndex] = source.passes[passIndex].dirtyRanges;
        }
        history.deltas.push_back(std::move(delta));
        constexpr size_t kUploadHistoryCapacity = GFX::kFrameResourceCount * 2u + 2u;
        if (history.deltas.size() > kUploadHistoryCapacity) {
            history.deltas.erase(history.deltas.begin());
        }
    }

    bool GpuDrivenLayer::PatchSceneUploadHistory(
        const GpuDrivenSceneSource& source,
        uintptr_t sourceIdentity,
        size_t instanceCount) {

        const auto& deltas = sceneUploadHistory_.deltas;
        const auto first = std::find_if(deltas.begin(), deltas.end(),
            [&](const SceneUploadDelta& delta) {
                return sceneBuffer_->CanPatchFrame(
                    sourceIdentity, instanceCount, source.layoutVersion, delta.baseVersion);
            });
        if (first == deltas.end()) {
            return false;
        }
        std::array<std::vector<GpuSceneDirtyRange>, kGpuDrivenPassCount> ranges{};
        uint64_t version = first->baseVersion;
        for (auto delta = first; delta != deltas.end(); ++delta) {
            if (delta->baseVersion != version) {
                return false;
            }
            for (size_t passIndex = 0; passIndex < kGpuDrivenPassCount; ++passIndex) {
                const auto& changes = delta->ranges[passIndex];
                ranges[passIndex].insert(ranges[passIndex].end(), changes.begin(), changes.end());
            }
            version = delta->sourceVersion;
        }
        if (version != source.sourceVersion) {
            return false;
        }

        // 現在のスロットが最後に使われてからの差分を全て補い、最新の値を一度だけ送る。
        sceneBuffer_->ReuseFrame(instanceCount);
        for (size_t passIndex = 0; passIndex < kGpuDrivenPassCount; ++passIndex) {
            if (!PatchDirtyPass(*sceneBuffer_, source.passes[passIndex], ranges[passIndex])) {
                return false;
            }
        }
        return true;
    }

    bool GpuDrivenLayer::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* staticRootSignature,
        UINT rootConstantParameterIndex,
        UINT rootConstantCount) {

        (void)device;
        (void)staticRootSignature;
        (void)rootConstantParameterIndex;
        (void)rootConstantCount;
        return sceneBuffer_ != nullptr ||
            traditionalCommandStreamBuffer_ != nullptr ||
            producer_ != nullptr;
    }

    void GpuDrivenLayer::Attach(
        SurfaceGpuSceneFrameBuffer* sceneBuffer,
        MassGpuSceneFrameBuffer* massSceneBuffer,
        GpuTraditionalCommandStreamBuffer* traditionalCommandStreamBuffer,
        IGpuDrivenProducer* producer,
        WATER::WaterSurfaceGpuFrameBuffer* waterSurfaceBuffer,
        SAND::SandSurfaceGpuFrameBuffer* sandSurfaceBuffer) {

        if (sceneBuffer_ != sceneBuffer) {
            sceneUploadHistory_ = {};
        }
        sceneBuffer_ = sceneBuffer;
        massSceneBuffer_ = massSceneBuffer;
        waterSurfaceBuffer_ = waterSurfaceBuffer;
        sandSurfaceBuffer_ = sandSurfaceBuffer;
        traditionalCommandStreamBuffer_ = traditionalCommandStreamBuffer;
        producer_ = producer;
        frameContext_.scene.instanceBuffer = sceneBuffer_;
        frameContext_.scene.massInstanceBuffer = massSceneBuffer_;
    }

    void GpuDrivenLayer::ResetFrame() {
        frameContext_ = {};
        frameContext_.scene.instanceBuffer = sceneBuffer_;
        frameContext_.scene.massInstanceBuffer = massSceneBuffer_;
        frameSource_ = nullptr;
        sceneUploadStats_ = {};
        commandFrameStats_ = {};
        traditionalCommandFrameKey_ = {};
        InitializePassExecutionStates(nullptr);
    }

    bool GpuDrivenLayer::BeginFrame(const GpuDrivenSceneSource* source) {
        ResetFrame();
        frameSource_ = source;
        frameContext_.stats.sourceInstanceCount =
            source != nullptr
                ? source->CountGpuSceneInstances()
                : 0u;
        InitializePassExecutionStates(source);
        RefreshPassExecutionStates();
        return true;
    }

    const GpuDrivenSceneUploadStats& GpuDrivenLayer::UploadSceneFrame(
        const GpuDrivenSceneUploadDesc& desc) {

        sceneUploadStats_ = {};
        frameContext_.scene.instanceBuffer = sceneBuffer_;
        frameContext_.scene.massInstanceBuffer = massSceneBuffer_;
        const bool waterSurfaceDataResident =
            waterSurfaceBuffer_ == nullptr ||
            waterSurfaceBuffer_->UploadFrame(
                desc.frameIndex,
                desc.commandList,
                frameSource_ != nullptr
                    ? frameSource_->waterSurfaceGpuData
                    : nullptr);
        sceneUploadStats_.waterSurfaceDataResident =
            waterSurfaceDataResident;

        const bool sandSurfaceDataResident =
            sandSurfaceBuffer_ == nullptr ||
            sandSurfaceBuffer_->UploadFrame(
                desc.frameIndex,
                desc.commandList,
                frameSource_ != nullptr
                    ? frameSource_->sandSurfaceGpuData
                    : nullptr,
                frameSource_ != nullptr
                    ? frameSource_->sandSurfaceHeights
                    : nullptr,
                frameSource_ != nullptr
                    ? frameSource_->sandSurfaceHeightVersion
                    : 0u);
        sceneUploadStats_.sandSurfaceDataResident =
            sandSurfaceDataResident;
        if (sceneBuffer_ != nullptr) {
            sceneBuffer_->BeginFrame(desc.frameIndex);
        }
        const GpuDrivenSceneSource* source = frameSource_;
        const bool massSceneResident =
            massSceneBuffer_ == nullptr
                ? source == nullptr || !source->HasMassInstances()
                : massSceneBuffer_->UploadFrame(
                    desc.frameIndex,
                    desc.commandList,
                    source != nullptr ? source->mass : nullptr,
                    {
                        desc.massMaterialSlotBySourceRecord,
                        desc.massMaterialSourceRecordCount
                    },
                    desc.massMaterialBindingVersion);
        sceneUploadStats_.massSceneResident = massSceneResident;
        if (massSceneBuffer_ != nullptr) {
            sceneUploadStats_.massBufferStats =
                massSceneBuffer_->GetStats();
        }
        frameContext_.scene.massInstanceCount =
            massSceneResident && source != nullptr && source->mass != nullptr
                ? NUMERIC::SaturateToUint32(
                    source->mass->instances.size())
                : 0u;
        InitializePassExecutionStates(source);
        RefreshPassExecutionStates();
        // surface payload と Scene records は同じ frame transaction。どちらかの
        // upload が失敗した frame へ、offset を持つ legacy instance record は
        // 送信しない。独立した Mass residency はこの失敗から隔離する。
        if (sceneBuffer_ == nullptr || source == nullptr ||
            !waterSurfaceDataResident || !sandSurfaceDataResident) {
            if (sceneBuffer_ != nullptr) {
                sceneBuffer_->ResetFrame();
                sceneUploadStats_.bufferStats = sceneBuffer_->GetStats();
            }
            if (desc.residency != nullptr) {
                desc.residency->Reset();
            }
            UploadSurfaceGpuSceneFrame(0u, 0u, 0u, 0u, 0u, 0u, false);
            frameContext_.stats.sourceInstanceCount +=
                frameContext_.scene.massInstanceCount;
            return sceneUploadStats_;
        }

        const GpuDrivenSceneSource& sceneSource = *source;
        sceneUploadStats_.sourceInstanceCount =
            sceneSource.sourceInstanceCount != 0u
                ? sceneSource.sourceInstanceCount
                : sceneSource.CountGpuSceneInstances();
        for (size_t passIndex = 0u;
            passIndex < kGpuDrivenPassCount;
            ++passIndex) {

            sceneUploadStats_.passInstanceCounts[passIndex] =
                NUMERIC::SaturateToUint32(
                    CountPassInstances(sceneSource.passes[passIndex]));
        }

        const uint64_t layoutVersion = sceneSource.layoutVersion;
        const uint64_t sourceVersion = sceneSource.sourceVersion;
        const uintptr_t sourceIdentity =
            reinterpret_cast<uintptr_t>(frameSource_);
        const size_t sourceInstanceCount =
            sceneUploadStats_.sourceInstanceCount;
        if (!SceneSourceLayoutTilesInstanceBuffer(
                sceneSource,
                sourceInstanceCount)) {
            static uint64_t warnedLayoutVersion = ~0ull;
            if (warnedLayoutVersion != layoutVersion) {
                warnedLayoutVersion = layoutVersion;
                HIKARI_LOG_WARN(
                    "[GpuDrivenLayer] SurfaceGpuScene pass regions do not tile "
                    "the instance buffer. Absolute instance indices will be "
                    "wrong for every pass after the first gap/overlap.");
            }
        }
        TrackSceneUploadDelta(sceneSource, sourceIdentity, sourceInstanceCount);
        const bool activeFrameResidentMatches =
            sceneBuffer_->CanReuseFrame(
                sourceIdentity,
                sourceInstanceCount,
                layoutVersion,
                sourceVersion);
        if (sourceInstanceCount == 0u) {
            sceneBuffer_->ResetFrame();
            if (desc.residency != nullptr) {
                desc.residency->Reset();
            }
        } else if (activeFrameResidentMatches) {
            sceneUploadStats_.reusedResidentFrame = true;
            sceneBuffer_->ReuseFrame(sourceInstanceCount);
        } else if (desc.allowDirtyRangePatching &&
            PatchSceneUploadHistory(sceneSource, sourceIdentity, sourceInstanceCount)) {
            sceneUploadStats_.patchedDirtyRanges = true;
        } else {
            UploadFullScene(*sceneBuffer_, sceneSource);
            sceneUploadStats_.uploadedFullScene = true;
        }

        bool sceneCommitted = true;
        if (sourceInstanceCount != 0u) {
            if (desc.materialSlotBySourceRecord != nullptr &&
                desc.materialSourceRecordCount != 0u) {
                sceneBuffer_->ApplyMaterialBindings(
                    {
                        desc.materialSlotBySourceRecord,
                        desc.materialSourceRecordCount
                    },
                    desc.materialBindingVersion);
            }
            sceneBuffer_->MarkResident(
                sourceIdentity,
                layoutVersion,
                sourceVersion,
                sourceInstanceCount);
            sceneCommitted = sceneBuffer_->CommitFrame(desc.commandList);
        }

        sceneUploadStats_.bufferStats = sceneBuffer_->GetStats();
        sceneUploadStats_.sceneResident =
            sceneCommitted &&
            sourceInstanceCount != 0u &&
            waterSurfaceDataResident &&
            sandSurfaceDataResident &&
            sceneUploadStats_.bufferStats.overflowInstanceCount == 0u &&
            sceneUploadStats_.bufferStats.uploadedInstanceCount ==
                sourceInstanceCount;
        if (desc.residency != nullptr) {
            if (sceneUploadStats_.sceneResident) {
                desc.residency->resident = true;
                desc.residency->layoutVersion = layoutVersion;
                desc.residency->sourceVersion = sourceVersion;
                desc.residency->instanceCount = sourceInstanceCount;
            } else {
                desc.residency->Reset();
            }
        }

        frameContext_.stats.sourceInstanceCount = sourceInstanceCount;
        UploadSurfaceGpuSceneFrame(
            NUMERIC::SaturateToUint32(sourceInstanceCount),
            ResolvePassBaseIndex(
                sceneSource.GetPass(GpuDrivenPassKind::ForwardOpaque)),
            ResolvePassBaseIndex(
                sceneSource.GetPass(GpuDrivenPassKind::DepthPrepass)),
            ResolvePassBaseIndex(
                sceneSource.GetPass(GpuDrivenPassKind::ForwardDepthAware)),
            ResolvePassBaseIndex(
                sceneSource.GetPass(GpuDrivenPassKind::ForwardTransparent)),
            ResolvePassBaseIndex(
                sceneSource.GetPass(GpuDrivenPassKind::Shadow)),
            sceneUploadStats_.sceneResident);
        InitializePassExecutionStates(&sceneSource);
        RefreshPassExecutionStates();
        frameContext_.stats.sourceInstanceCount +=
            frameContext_.scene.massInstanceCount;
        return sceneUploadStats_;
    }

    void GpuDrivenLayer::UploadSurfaceGpuSceneFrame(
        uint32_t instanceCount,
        uint32_t opaqueBaseIndex,
        uint32_t depthPrepassBaseIndex,
        uint32_t depthAwareBaseIndex,
        uint32_t transparentBaseIndex,
        uint32_t shadowBaseIndex,
        bool sceneResident) {

        frameContext_.scene.instanceBuffer = sceneBuffer_;
        frameContext_.scene.instanceCount = instanceCount;
        frameContext_.scene.opaqueBaseIndex = opaqueBaseIndex;
        frameContext_.scene.depthPrepassBaseIndex = depthPrepassBaseIndex;
        frameContext_.scene.depthAwareBaseIndex = depthAwareBaseIndex;
        frameContext_.scene.transparentBaseIndex = transparentBaseIndex;
        frameContext_.scene.shadowBaseIndex = shadowBaseIndex;
        frameContext_.stats.residentInstanceCount = instanceCount;
        frameContext_.stats.sceneResident = sceneResident;
    }

    bool GpuDrivenLayer::CommitSurfaceGpuSceneMaterialFrame(
        ID3D12GraphicsCommandList* commandList) {

        if (sceneBuffer_ != nullptr) {
            return sceneBuffer_->CommitFrame(commandList);
        }
        return false;
    }

    void GpuDrivenLayer::ImportProducerOutput(
        const GpuDrivenProducerFrameOutput& output) {

        frameContext_.visibility = output.visibility;
        frameContext_.commands = output.commands;
        frameContext_.stats.visibilityReady = output.visibilityReady;
        frameContext_.stats.visibilitySeedCount = output.visibilitySeedCount;
        frameContext_.stats.visibilityOverflowInstanceCount =
            output.visibilityOverflowInstanceCount;
        frameContext_.stats.commandBuildReady =
            output.commandBuildReady;
        RefreshPassExecutionStates();
    }

    void GpuDrivenLayer::SetBackendAvailability(
        const GpuDrivenBackendAvailability& availability) {

        frameContext_.backendAvailability = availability;
        RefreshPassExecutionStates();
    }

    const GpuDrivenCommandFrameStats& GpuDrivenLayer::BuildCommandFrame(
        const GpuDrivenCommandFrameDesc& desc) {

        commandFrameStats_ = {};
        const bool buildTraditionalStream =
            HasTraditionalCommandStreamWork(frameSource_);

        TraditionalCommandFrameKey key{};
        key.valid = true;
        key.frameIndex = desc.frameIndex;
        key.source = frameSource_;
        key.sourceVersion =
            frameSource_ != nullptr ? frameSource_->sourceVersion : 0u;
        key.layoutVersion =
            frameSource_ != nullptr ? frameSource_->layoutVersion : 0u;
        key.hasCullViewProj = desc.cullViewProj != nullptr;
        key.enableSurfaceFrustumCull = desc.enableSurfaceFrustumCull;
        if (desc.cullViewProj != nullptr) {
            key.cullViewProj = *desc.cullViewProj;
        }
        const bool reuseTraditionalCommands =
            buildTraditionalStream &&
            traditionalCommandStreamBuffer_ != nullptr &&
            traditionalCommandFrameKey_.valid &&
            traditionalCommandFrameKey_.frameIndex == key.frameIndex &&
            traditionalCommandFrameKey_.source == key.source &&
            traditionalCommandFrameKey_.sourceVersion == key.sourceVersion &&
            traditionalCommandFrameKey_.layoutVersion == key.layoutVersion &&
            traditionalCommandFrameKey_.hasCullViewProj == key.hasCullViewProj &&
            traditionalCommandFrameKey_.enableSurfaceFrustumCull ==
                key.enableSurfaceFrustumCull &&
            std::memcmp(
                &traditionalCommandFrameKey_.cullViewProj,
                &key.cullViewProj,
                sizeof(key.cullViewProj)) == 0;

        if (traditionalCommandStreamBuffer_ != nullptr) {
            traditionalCommandStreamBuffer_->BeginFrame(desc.frameIndex);
        }

        if (traditionalCommandStreamBuffer_ != nullptr && buildTraditionalStream) {
            if (!reuseTraditionalCommands && frameSource_ != nullptr) {
                const bool reusedResidentInput =
                    traditionalCommandStreamBuffer_->PrepareCommandInputs(
                        reinterpret_cast<uintptr_t>(frameSource_),
                        frameSource_->layoutVersion,
                        frameSource_->sourceVersion);
                if (!reusedResidentInput) {
                    UploadTraditionalIndirectCommands(
                        *traditionalCommandStreamBuffer_,
                        *frameSource_);
                }
                if (desc.cullViewProj != nullptr) {
                    (void)traditionalCommandStreamBuffer_->BuildGpuCompactedCommands(
                        desc.commandList,
                        *desc.cullViewProj,
                        desc.enableSurfaceFrustumCull);
                }
                traditionalCommandFrameKey_ = key;
            }
            commandFrameStats_.traditionalCommandStreamStats =
                traditionalCommandStreamBuffer_->GetStats();
        } else {
            if (traditionalCommandStreamBuffer_ != nullptr &&
                !reuseTraditionalCommands) {
                traditionalCommandStreamBuffer_->ResetFrame();
            }
            traditionalCommandFrameKey_ = {};
        }

        if (desc.publishCommandBuffers) {
            BuildCommandBuffers();
            commandFrameStats_.commandFramePublished = true;
        }
        return commandFrameStats_;
    }

    void GpuDrivenLayer::BuildCommandBuffers() {
        frameContext_.commands.surfaceDrawIndexedArgs =
            traditionalCommandStreamBuffer_ != nullptr
                ? traditionalCommandStreamBuffer_->GetArgumentBuffer()
                : nullptr;
        frameContext_.commands.surfaceSkinnedDrawIndexedArgs =
            traditionalCommandStreamBuffer_ != nullptr
                ? traditionalCommandStreamBuffer_->GetSkinnedArgumentBuffer()
                : nullptr;
        frameContext_.commands.surfaceDrawIndexedCounter =
            traditionalCommandStreamBuffer_ != nullptr
                ? traditionalCommandStreamBuffer_->GetCounterBuffer()
                : nullptr;
        frameContext_.commands.surfaceDrawIndexedSignature =
            traditionalCommandStreamBuffer_ != nullptr
                ? traditionalCommandStreamBuffer_->GetCommandSignature()
                : nullptr;
        frameContext_.commands.surfaceSkinnedDrawIndexedSignature =
            traditionalCommandStreamBuffer_ != nullptr
                ? traditionalCommandStreamBuffer_->GetSkinnedCommandSignature()
                : nullptr;

        frameContext_.stats.commandBuildReady =
            frameContext_.commands.meshDispatchArgs != nullptr ||
            (frameContext_.commands.surfaceDrawIndexedArgs != nullptr &&
                frameContext_.commands.surfaceDrawIndexedCounter != nullptr) ||
            (frameContext_.commands.surfaceSkinnedDrawIndexedArgs != nullptr &&
                frameContext_.commands.surfaceDrawIndexedCounter != nullptr);
        RefreshPassExecutionStates();
    }

    bool GpuDrivenLayer::IsBackendConsumable(
        GpuDrivenPassKind pass,
        GeometryBackendKind backend) const {

        const GpuDrivenPassExecutionState& state =
            GetPassExecutionState(pass);
        switch (backend) {
        case GeometryBackendKind::GpuDrivenMeshShader:
            return state.meshShaderConsumable;
        case GeometryBackendKind::GpuDrivenTraditionalVsPs:
            return state.traditionalIndirectConsumable;
        default:
            return false;
        }
    }

    bool GpuDrivenLayer::IsPassGpuReady(GpuDrivenPassKind pass) const {
        return GetPassExecutionState(pass).gpuBackendReady;
    }

    GeometryBackendExecutionPlan GpuDrivenLayer::GetPassExecutionPlan(
        GpuDrivenPassKind pass) const {

        const GeometryBackendPolicy policy =
            ResolveGeometryBackendPolicy(pass);
        GeometryBackendExecutionPlan plan{};

        const bool preferredReady =
            IsBackendConsumable(pass, policy.preferred);
        const bool secondaryReady =
            IsBackendConsumable(pass, policy.secondary);
        const bool sidecarReady =
            IsBackendConsumable(
                pass,
                GeometryBackendKind::GpuDrivenTraditionalVsPs);

        if (preferredReady) {
            plan.AddGpuBackend(policy.preferred);
            if (policy.preferred ==
                GeometryBackendKind::GpuDrivenMeshShader &&
                sidecarReady) {
                plan.AddGpuBackend(
                    GeometryBackendKind::GpuDrivenTraditionalVsPs);
            }
            return plan;
        }

        if (!policy.forcePreferredOnly && secondaryReady) {
            plan.AddGpuBackend(policy.secondary);
        }
        return plan;
    }

    GeometryBackendContext GpuDrivenLayer::BuildGeometryBackendContext(
        ID3D12GraphicsCommandList* commandList,
        GpuDrivenPassKind pass,
        GeometryBackendKind backend) const {

        GeometryBackendContext context{};
        context.commandList = commandList;
        context.requestedPass = pass;
        const GpuDrivenPassExecutionState& state =
            GetPassExecutionState(pass);
        context.pass = state.visibilityPass;
        context.backend = backend;
        context.scene = &frameContext_.scene;
        context.visibility = &frameContext_.visibility;
        context.commands = &frameContext_.commands;
        context.drawCommandRange =
            frameContext_.drawStream.FindRange(pass, backend);
        context.traditionalIndirect =
            context.drawCommandRange != nullptr
                ? context.drawCommandRange->traditionalIndirect
                : nullptr;
        return context;
    }

    const GpuDrivenPassExecutionState&
        GpuDrivenLayer::GetPassExecutionState(
            GpuDrivenPassKind pass) const {

        return frameContext_.passExecution[ToPassIndex(pass)];
    }

    SurfaceGpuSceneFrameBuffer* GpuDrivenLayer::GetSceneBuffer() const {
        return sceneBuffer_;
    }

    WATER::WaterSurfaceGpuFrameBuffer*
        GpuDrivenLayer::GetWaterSurfaceBuffer() const {

        return waterSurfaceBuffer_;
    }

    GpuTraditionalCommandStreamBuffer* GpuDrivenLayer::GetTraditionalCommandStreamBuffer() const {
        return traditionalCommandStreamBuffer_;
    }

    IGpuDrivenProducer* GpuDrivenLayer::GetProducer() const {
        return producer_;
    }

    const GpuDrivenFrameContext& GpuDrivenLayer::GetFrameContext() const {
        return frameContext_;
    }

    const GpuDrivenDrawCommandStream&
        GpuDrivenLayer::GetDrawCommandStream() const {

        return frameContext_.drawStream;
    }

    const GpuDrivenSceneUploadStats&
        GpuDrivenLayer::GetSceneUploadStats() const {

        return sceneUploadStats_;
    }

    const GpuDrivenCommandFrameStats&
        GpuDrivenLayer::GetCommandFrameStats() const {

        return commandFrameStats_;
    }

    void GpuDrivenLayer::InitializePassExecutionStates(
        const GpuDrivenSceneSource* source) {

        for (size_t passIndex = 0; passIndex < kGpuDrivenPassCount; ++passIndex) {
            GpuDrivenPassExecutionState& state =
                frameContext_.passExecution[passIndex];
            state = {};
            state.pass = PassFromIndex(passIndex);
            state.sourcePass = ResolveExecutionSourcePass(state.pass);
            state.visibilityPass = state.sourcePass;
            state.sourceMode =
                state.sourcePass == state.pass
                    ? GpuDrivenPassSourceMode::Own
                    : GpuDrivenPassSourceMode::Derived;
            if (source == nullptr) {
                state.sourceMode = GpuDrivenPassSourceMode::None;
                continue;
            }

            const GpuDrivenPassSource& passSource =
                source->GetPass(state.sourcePass);
            const bool legacySource =
                sceneUploadStats_.sceneResident &&
                passSource.HasGpuSceneRange();
            const bool massSource =
                sceneUploadStats_.massSceneResident &&
                source->HasMassInstances() &&
                (state.sourcePass == GpuDrivenPassKind::ForwardOpaque ||
                 state.sourcePass == GpuDrivenPassKind::DepthPrepass);
            state.hasSource = legacySource || massSource;
            state.clusterEligible =
                (legacySource && passSource.clusterEligible) || massSource;
            state.sourceInstanceCount =
                (legacySource
                    ? static_cast<size_t>(passSource.gpuSceneInstanceCount)
                    : 0u) +
                (massSource
                    ? source->mass->instances.size()
                    : 0u);
            state.hasTraditionalIndirectCommands =
                legacySource &&
                passSource.traditionalIndirect.HasCommands();
            state.traditionalIndirectCommandCount =
                passSource.traditionalIndirect.CommandCount();
            state.traditionalIndirectInstanceCount =
                passSource.traditionalIndirect.gpuSceneInstanceCount;
            if (!state.hasSource) {
                state.sourceMode = GpuDrivenPassSourceMode::None;
            }
        }
    }

    void GpuDrivenLayer::RefreshPassExecutionStates() {
        for (GpuDrivenPassExecutionState& state :
            frameContext_.passExecution) {

            const GpuVisibilityPassResult& visibility =
                frameContext_.visibility.GetPass(state.visibilityPass);
            const GpuDrivenPassKind commandPass = state.visibilityPass;
            const bool hasBucketCapacity =
                HasCommandBucketCapacity(frameContext_.commands, commandPass);
            const GpuDrivenCommandPassLayout& commandLayout =
                frameContext_.commands.layout.GetPass(commandPass);

            state.drawSeedCount = visibility.submittedDrawSeedCount;
            state.hasDrawSeeds = state.drawSeedCount != 0;
            state.visibleCommandCount =
                visibility.CountKnownVisibleCommands();
            state.visibleCommandOverflowCount =
                visibility.CountKnownVisibleCommandOverflows();
            state.visibleCommandCountKnown =
                visibility.HasKnownVisibleCommandCounts();
            state.gpuCommandCounterBacked =
                visibility.HasGpuCommandCounters();
            state.gpuCommandBucketCapacity =
                commandLayout.commandBucketCapacity;
            state.overflowBlocked =
                frameContext_.stats.visibilityOverflowInstanceCount != 0;
            state.visibilityReady =
                frameContext_.stats.visibilityReady &&
                state.hasDrawSeeds &&
                frameContext_.visibility.counterBuffer != nullptr &&
                state.gpuCommandCounterBacked &&
                !state.overflowBlocked;
            state.commandBuildReady =
                frameContext_.stats.commandBuildReady &&
                hasBucketCapacity;

            const bool useGeometryPipeline =
                UsesGeometryAuxPipeline(state.pass);
            const bool meshPipelineReady =
                useGeometryPipeline
                    ? frameContext_.backendAvailability.meshShaderGeometryAuxPipelineReady
                    : frameContext_.backendAvailability.meshShaderForwardPipelineReady;
            state.meshShaderConsumable =
                state.sourceInstanceCount != 0 &&
                state.clusterEligible &&
                state.visibilityReady &&
                state.commandBuildReady &&
                frameContext_.commands.meshDispatchArgs != nullptr &&
                frameContext_.commands.meshDispatchSignature != nullptr &&
                meshPipelineReady;
            state.traditionalIndirectConsumable =
                state.hasSource &&
                state.hasTraditionalIndirectCommands &&
                traditionalCommandStreamBuffer_ != nullptr &&
                traditionalCommandStreamBuffer_->HasGpuCompactedCommands() &&
                (frameContext_.commands.surfaceDrawIndexedArgs != nullptr ||
                    frameContext_.commands.surfaceSkinnedDrawIndexedArgs != nullptr) &&
                frameContext_.commands.surfaceDrawIndexedCounter != nullptr &&
                (frameContext_.commands.surfaceDrawIndexedSignature != nullptr ||
                    frameContext_.commands.surfaceSkinnedDrawIndexedSignature != nullptr) &&
                frameContext_.backendAvailability.traditionalIndirectPipelineReady;
            state.gpuBackendReady =
                state.meshShaderConsumable ||
                state.traditionalIndirectConsumable;
        }
        RebuildDrawCommandStream();
    }

    void GpuDrivenLayer::RebuildDrawCommandStream() {
        frameContext_.drawStream.Reset();
        if (frameSource_ == nullptr) {
            return;
        }

        for (const GpuDrivenPassExecutionState& state :
            frameContext_.passExecution) {

            if (state.sourceMode == GpuDrivenPassSourceMode::None) {
                continue;
            }

            const GpuDrivenPassSource& passSource =
                frameSource_->GetPass(state.sourcePass);
            const GpuDrivenCommandPassLayout& commandLayout =
                frameContext_.commands.layout.GetPass(state.visibilityPass);

            if (state.meshShaderConsumable) {
                GpuDrivenDrawCommandRange range{};
                range.pass = state.pass;
                range.sourcePass = state.sourcePass;
                range.backend = GeometryBackendKind::GpuDrivenMeshShader;
                range.producer = GpuDrivenCommandProducerKind::GpuCommandBuilder;
                range.gpuCommandLayout = &commandLayout;
                range.argumentBuffer = frameContext_.commands.meshDispatchArgs;
                range.commandSignature =
                    frameContext_.commands.meshDispatchSignature;
                range.commandCount = state.drawSeedCount;
                range.visibleCommandCount = state.visibleCommandCount;
                range.visibleCommandOverflowCount =
                    state.visibleCommandOverflowCount;
                range.commandBucketCapacity =
                    state.gpuCommandBucketCapacity;
                range.gpuSceneBaseIndex = passSource.gpuSceneBaseIndex;
                range.instanceCount = state.sourceInstanceCount;
                range.consumable = true;
                range.gpuAuthored = true;
                range.gpuCounterBacked = state.gpuCommandCounterBacked;
                range.visibleCommandCountKnown =
                    state.visibleCommandCountKnown;
                frameContext_.drawStream.SetRange(range);
            }

            if (state.traditionalIndirectConsumable) {
                GpuDrivenDrawCommandRange range{};
                range.pass = state.pass;
                range.sourcePass = state.sourcePass;
                range.backend = GeometryBackendKind::GpuDrivenTraditionalVsPs;
                range.producer = GpuDrivenCommandProducerKind::GpuCompactedIndirect;
                range.traditionalIndirect = &passSource.traditionalIndirect;
                range.argumentBuffer = frameContext_.commands.surfaceDrawIndexedArgs;
                range.skinnedArgumentBuffer =
                    frameContext_.commands.surfaceSkinnedDrawIndexedArgs;
                range.counterBuffer = frameContext_.commands.surfaceDrawIndexedCounter;
                range.commandSignature =
                    frameContext_.commands.surfaceDrawIndexedSignature;
                range.skinnedCommandSignature =
                    frameContext_.commands.surfaceSkinnedDrawIndexedSignature;
                range.argumentBufferOffset =
                    traditionalCommandStreamBuffer_ != nullptr
                        ? traditionalCommandStreamBuffer_->GetArgumentBufferOffset(state.sourcePass)
                        : 0u;
                range.skinnedArgumentBufferOffset =
                    traditionalCommandStreamBuffer_ != nullptr
                        ? traditionalCommandStreamBuffer_->GetSkinnedArgumentBufferOffset(state.sourcePass)
                        : 0u;
                range.counterBufferOffset =
                    traditionalCommandStreamBuffer_ != nullptr
                        ? traditionalCommandStreamBuffer_->GetCommandCounterOffset(state.sourcePass)
                        : 0u;
                range.skinnedCounterBufferOffset =
                    traditionalCommandStreamBuffer_ != nullptr
                        ? traditionalCommandStreamBuffer_->GetSkinnedCommandCounterOffset(state.sourcePass)
                        : 0u;
                range.argumentBucketStride =
                    traditionalCommandStreamBuffer_ != nullptr
                        ? traditionalCommandStreamBuffer_->GetArgumentBucketStride()
                        : 0u;
                range.skinnedArgumentBucketStride =
                    traditionalCommandStreamBuffer_ != nullptr
                        ? traditionalCommandStreamBuffer_->GetSkinnedArgumentBucketStride()
                        : 0u;
                range.counterBucketStride =
                    traditionalCommandStreamBuffer_ != nullptr
                        ? traditionalCommandStreamBuffer_->GetCounterBucketStride()
                        : 0u;
                range.commandBucketCapacity =
                    traditionalCommandStreamBuffer_ != nullptr
                        ? traditionalCommandStreamBuffer_->GetCommandBucketCapacity()
                        : 0u;
                const size_t traditionalBucketCapacity =
                    traditionalCommandStreamBuffer_ != nullptr
                        ? traditionalCommandStreamBuffer_->GetCommandBucketCount()
                        : 0u;
                const size_t traditionalBucketCount =
                    passSource.traditionalIndirect.bucketVariants != nullptr
                        ? passSource.traditionalIndirect.bucketVariants->size()
                        : 0u;
                range.commandBucketCount =
                    (std::min)(traditionalBucketCount, traditionalBucketCapacity);
                range.gpuSceneBaseIndex =
                    passSource.traditionalIndirect.gpuSceneBaseIndex;
                range.commandCount = state.traditionalIndirectCommandCount;
                range.staticCommandCount =
                    passSource.traditionalIndirect.staticCommandCount;
                range.skinnedCommandCount =
                    passSource.traditionalIndirect.skinnedCommandCount;
                range.recordCount = range.commandCount;
                range.instanceCount = state.traditionalIndirectInstanceCount;
                range.consumable = true;
                range.gpuAuthored = true;
                range.gpuCounterBacked = true;
                range.visibleCommandCountKnown = false;
                frameContext_.drawStream.SetRange(range);
            }
        }
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
