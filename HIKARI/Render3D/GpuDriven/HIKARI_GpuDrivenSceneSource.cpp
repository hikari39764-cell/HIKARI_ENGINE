#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"

#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    void GpuDrivenTraditionalIndirectView::Reset() {
        records = nullptr;
        executableRecordIndices = nullptr;
        commands = nullptr;
        instances = nullptr;
        materialSources = nullptr;
        jointPalettes = nullptr;
        bucketVariants = nullptr;
        gpuSceneBaseIndex = 0;
        gpuSceneInstanceCount = 0;
        staticCommandCount = 0;
        skinnedCommandCount = 0;
    }

    bool GpuDrivenTraditionalIndirectView::HasCommands() const {
        if (records == nullptr ||
            executableRecordIndices == nullptr ||
            commands == nullptr ||
            instances == nullptr ||
            materialSources == nullptr ||
            jointPalettes == nullptr ||
            bucketVariants == nullptr ||
            bucketVariants->empty() ||
            commands->empty()) {
            return false;
        }

        const size_t commandCount = commands->size();
        return
            records->size() == commandCount &&
            executableRecordIndices->size() == commandCount &&
            instances->size() == commandCount &&
            materialSources->size() == commandCount &&
            jointPalettes->size() == commandCount &&
            static_cast<size_t>(staticCommandCount) +
                static_cast<size_t>(skinnedCommandCount) == commandCount;
    }

    bool GpuDrivenTraditionalIndirectView::HasGpuSceneRange() const {
        return gpuSceneInstanceCount != 0u;
    }

    bool GpuDrivenTraditionalIndirectView::HasGpuSceneInstances() const {
        return instances != nullptr && !instances->empty();
    }

    size_t GpuDrivenTraditionalIndirectView::CommandCount() const {
        return commands != nullptr ? commands->size() : 0u;
    }

    void GpuDrivenPassSource::Reset() {
        instances = nullptr;
        materialSources = nullptr;
        gpuSceneBaseIndex = 0;
        gpuSceneInstanceCount = 0;
        preferredBackend = GpuDrivenBackendKind::TraditionalIndirect;
        clusterEligible = false;
        traditionalIndirect.Reset();
        dirtyRanges.clear();
    }

    bool GpuDrivenPassSource::HasPrimaryGpuSceneRange() const {
        return gpuSceneInstanceCount != 0u;
    }

    bool GpuDrivenPassSource::HasPrimaryGpuSceneInstances() const {
        return instances != nullptr && !instances->empty();
    }

    bool GpuDrivenPassSource::HasGpuSceneRange() const {
        return
            HasPrimaryGpuSceneRange() ||
            traditionalIndirect.HasGpuSceneRange();
    }

    bool GpuDrivenPassSource::HasGpuSceneInstances() const {
        return
            HasPrimaryGpuSceneInstances() ||
            traditionalIndirect.HasGpuSceneInstances();
    }

    bool GpuDrivenPassSource::HasDirtyGpuSceneRanges() const {
        for (const GpuSceneDirtyRange& range : dirtyRanges) {
            if (range.IsValid()) {
                return true;
            }
        }
        return false;
    }

    GpuDrivenPassBuildInput GpuDrivenPassSource::ToFrameBuildInput() const {
        GpuDrivenPassBuildInput input{};
        input.instances = instances;
        input.gpuSceneBaseIndex = gpuSceneBaseIndex;
        input.gpuSceneInstanceCount = gpuSceneInstanceCount;
        input.preferredBackend = preferredBackend;
        input.clusterEligible = clusterEligible;
        return input;
    }

    void GpuDrivenSceneSource::Reset() {
        for (GpuDrivenPassSource& pass : passes) {
            pass.Reset();
        }
        meshShaderJointPalettes = nullptr;
        layoutVersion = 0;
        sourceVersion = 0;
        dirtyBaseSourceVersion = 0;
        sourceInstanceCount = 0;
        sourceRecordCount = 0;
    }

    GpuDrivenPassSource& GpuDrivenSceneSource::GetPass(
        GpuDrivenPassKind passKind) {

        return passes[ToPassIndex(passKind)];
    }

    const GpuDrivenPassSource& GpuDrivenSceneSource::GetPass(
        GpuDrivenPassKind passKind) const {

        return passes[ToPassIndex(passKind)];
    }

    bool GpuDrivenSceneSource::HasAnyGpuSceneRanges() const {
        for (const GpuDrivenPassSource& pass : passes) {
            if (pass.HasGpuSceneRange()) {
                return true;
            }
        }
        return false;
    }

    bool GpuDrivenSceneSource::HasAnyGpuSceneInstances() const {
        return HasAnyGpuSceneRanges();
    }

    bool GpuDrivenSceneSource::HasAnyDirtyGpuSceneRanges() const {
        for (const GpuDrivenPassSource& pass : passes) {
            if (pass.HasDirtyGpuSceneRanges()) {
                return true;
            }
        }
        return false;
    }

    size_t GpuDrivenSceneSource::CountGpuSceneInstances() const {
        if (sourceInstanceCount != 0u) {
            return sourceInstanceCount;
        }

        size_t count = 0;
        for (const GpuDrivenPassSource& pass : passes) {
            if (pass.HasGpuSceneRange()) {
                count += pass.gpuSceneInstanceCount;
                count += pass.traditionalIndirect.gpuSceneInstanceCount;
            }
        }
        return count;
    }

    GpuDrivenFrameBuildInput BuildGpuDrivenFrameInput(
        const GpuDrivenSceneSource& source) {

        GpuDrivenFrameBuildInput input{};
        for (size_t i = 0; i < kGpuDrivenPassCount; ++i) {
            input.passes[i] = source.passes[i].ToFrameBuildInput();
        }
        return input;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
