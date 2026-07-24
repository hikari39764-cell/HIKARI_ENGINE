#include "Render3D/GpuDriven/CommandStream/HIKARI_OwnedTraditionalIndirectStream.h"

#include <algorithm>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    void OwnedTraditionalIndirectStream::Clear() {
        records.clear();
        executableRecordIndices.clear();
        commands.clear();
        instances.clear();
        materialSources.clear();
        jointPalettes.clear();
        bucketVariants.clear();
        gpuSceneBaseIndex = 0;
        gpuSceneInstanceCount = 0;
        staticCommandCount = 0;
        skinnedCommandCount = 0;
    }

    bool OwnedTraditionalIndirectStream::CopyFrom(
        const GpuDrivenTraditionalIndirectView& view) {

        Clear();
        if (!view.HasCommands()) {
            return false;
        }

        records = *view.records;
        executableRecordIndices = *view.executableRecordIndices;
        commands = *view.commands;
        if (view.instances != nullptr) {
            instances = *view.instances;
        }
        if (view.materialSources != nullptr) {
            materialSources = *view.materialSources;
        }
        if (view.jointPalettes != nullptr) {
            jointPalettes = *view.jointPalettes;
        }
        if (view.bucketVariants != nullptr) {
            bucketVariants = *view.bucketVariants;
        }
        gpuSceneBaseIndex = view.gpuSceneBaseIndex;
        gpuSceneInstanceCount = view.gpuSceneInstanceCount;
        staticCommandCount = view.staticCommandCount;
        skinnedCommandCount = view.skinnedCommandCount;
        return true;
    }

    bool OwnedTraditionalIndirectStream::CopyRangeFrom(
        const GpuDrivenTraditionalIndirectView& view,
        size_t firstCommand,
        size_t commandCount,
        bool skinnedRange) {

        Clear();
        if (!view.HasCommands() ||
            view.records == nullptr ||
            view.executableRecordIndices == nullptr ||
            view.commands == nullptr ||
            view.instances == nullptr ||
            view.materialSources == nullptr ||
            view.jointPalettes == nullptr ||
            firstCommand >= view.commands->size()) {
            return false;
        }

        const size_t endCommand =
            (std::min)(
                view.commands->size(),
                firstCommand + commandCount);
        if (endCommand <= firstCommand) {
            return false;
        }

        records.reserve(endCommand - firstCommand);
        executableRecordIndices.reserve(
            endCommand - firstCommand);
        commands.reserve(endCommand - firstCommand);
        instances.reserve(endCommand - firstCommand);
        materialSources.reserve(endCommand - firstCommand);
        jointPalettes.reserve(endCommand - firstCommand);
        if (view.bucketVariants != nullptr) {
            bucketVariants = *view.bucketVariants;
        }

        for (size_t sourceIndex = firstCommand;
            sourceIndex < endCommand;
            ++sourceIndex) {

            const uint32_t localIndex =
                static_cast<uint32_t>(records.size());
            records.push_back((*view.records)[sourceIndex]);
            executableRecordIndices.push_back(localIndex);

            RUNTIME::SurfaceGpuSceneInstance instance =
                (*view.instances)[sourceIndex];
            instance.sourceRecordIndex = localIndex;
            instances.push_back(instance);

            RUNTIME::SurfaceGpuSceneMaterialSource material =
                (*view.materialSources)[sourceIndex];
            material.localGpuSceneInstanceIndex = localIndex;
            material.sourceRecordIndex = localIndex;
            materialSources.push_back(material);

            jointPalettes.push_back(
                (*view.jointPalettes)[sourceIndex]);

            RUNTIME::SurfaceDrawCommand command =
                (*view.commands)[sourceIndex];
            command.firstExecutableIndex = localIndex;
            command.firstRecordIndex = localIndex;
            command.firstGpuSceneInstanceIndex = localIndex;
            commands.push_back(command);
        }

        gpuSceneBaseIndex = 0;
        gpuSceneInstanceCount =
            static_cast<uint32_t>(instances.size());
        if (skinnedRange) {
            skinnedCommandCount =
                static_cast<uint32_t>(commands.size());
        } else {
            staticCommandCount =
                static_cast<uint32_t>(commands.size());
        }
        return true;
    }

    GpuDrivenTraditionalIndirectView
        OwnedTraditionalIndirectStream::GetView() const {

        GpuDrivenTraditionalIndirectView view{};
        if (commands.empty() ||
            records.empty() ||
            executableRecordIndices.empty()) {
            return view;
        }

        view.records = &records;
        view.executableRecordIndices =
            &executableRecordIndices;
        view.commands = &commands;
        view.instances =
            instances.empty() ? nullptr : &instances;
        view.materialSources =
            materialSources.empty()
                ? nullptr
                : &materialSources;
        view.jointPalettes =
            jointPalettes.empty() ? nullptr : &jointPalettes;
        view.bucketVariants =
            bucketVariants.empty() ? nullptr : &bucketVariants;
        view.gpuSceneBaseIndex = gpuSceneBaseIndex;
        view.gpuSceneInstanceCount = gpuSceneInstanceCount;
        view.staticCommandCount = staticCommandCount;
        view.skinnedCommandCount = skinnedCommandCount;
        return view;
    }

    void OwnedTraditionalIndirectStream::AttachTo(
        GpuDrivenPassSource& pass) const {

        pass.traditionalIndirect = GetView();
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
