#include "Render3D/Core/MeshRenderer/Execution/HIKARI_MeshTraditionalIndirectOwner.h"

#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Render3D/Core/MeshRenderer/Resources/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/HIKARI_Mesh.h"

namespace HIKARI::MESHRENDERER {

    void MeshTraditionalIndirectOwner::Clear() {
        for (OwnedStream& stream : streams_) {
            stream.Clear();
        }
    }

    bool MeshTraditionalIndirectOwner::HasSourceCommands(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) const {

        for (const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass : source.passes) {
            if (pass.traditionalIndirect.HasCommands()) {
                return true;
            }
        }
        return false;
    }

    void MeshTraditionalIndirectOwner::CopyFromSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) {

        for (size_t passIndex = 0;
            passIndex < RENDER3D::GPUDRIVEN::kGpuDrivenPassCount;
            ++passIndex) {

            (void)streams_[passIndex].CopyFrom(
                source.passes[passIndex].traditionalIndirect);
        }
    }

    void MeshTraditionalIndirectOwner::AttachToSceneSource(
        RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) const {

        for (size_t passIndex = 0;
            passIndex < RENDER3D::GPUDRIVEN::kGpuDrivenPassCount;
            ++passIndex) {

            streams_[passIndex].AttachTo(source.passes[passIndex]);
        }
    }

    void MeshTraditionalIndirectOwner::HydrateStream(
        OwnedStream& stream,
        const MeshRendererTraditionalIndirectHydrationContext& context) {

        if (stream.commands.empty() ||
            stream.records.empty() ||
            stream.executableRecordIndices.empty()) {
            return;
        }

        for (RENDER3D::RUNTIME::SurfaceDrawCommand& command : stream.commands) {
            command.triangleMeshView = {};
            command.jointPaletteGpuAddress = 0;
            if (command.recordCount == 0 ||
                command.firstExecutableIndex >= stream.executableRecordIndices.size()) {
                continue;
            }

            const uint32_t recordIndex =
                stream.executableRecordIndices[command.firstExecutableIndex];
            if (recordIndex >= stream.records.size()) {
                continue;
            }

            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record =
                stream.records[recordIndex];
            if (record.model == nullptr ||
                record.meshIndex >= record.model->meshes.size()) {
                continue;
            }

            const MeshAsset& meshAsset = record.model->meshes[record.meshIndex];
            if (record.primitiveIndex >= meshAsset.primitives.size() ||
                context.device == nullptr ||
                context.primitiveCache == nullptr) {
                continue;
            }

            const bool hasJointPalette =
                command.firstRecordIndex !=
                    RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex &&
                command.firstRecordIndex < stream.jointPalettes.size() &&
                !stream.jointPalettes[command.firstRecordIndex].empty();
            const bool skinnedCommand = record.skinned && hasJointPalette;
            const MeshPrimitive& primitive =
                meshAsset.primitives[record.primitiveIndex];
            Mesh* mesh = skinnedCommand
                ? context.primitiveCache->GetOrCreateSkinned(
                    context.device,
                    primitive,
                    context.debugStats)
                : context.primitiveCache->GetOrCreateStatic(
                    context.device,
                    primitive,
                    context.debugStats);
            if (mesh == nullptr || !mesh->IsValid()) {
                continue;
            }

            command.triangleMeshView.vertexBuffer = mesh->GetVBView();
            command.triangleMeshView.indexBuffer = mesh->GetIBView();
            if (!command.HasTriangleMeshGpuView() || !skinnedCommand) {
                continue;
            }

            const size_t paletteSlot =
                static_cast<size_t>(stream.gpuSceneBaseIndex) +
                static_cast<size_t>(command.firstGpuSceneInstanceIndex);
            if (paletteSlot >= kMaxObjectCount ||
                context.jointPaletteMapped == nullptr ||
                context.jointPaletteBuffer == nullptr) {
                command.jointPaletteGpuAddress = 0;
                continue;
            }

            (void)UploadJointPalette(
                context.jointPaletteMapped,
                paletteSlot,
                stream.jointPalettes[command.firstRecordIndex]);
            constexpr UINT kJointPaletteStride =
                GFX::AlignD3D12ConstantBufferByteSize(sizeof(JointPaletteCB));
            command.jointPaletteGpuAddress =
                context.jointPaletteBuffer->GetGPUVirtualAddress() +
                static_cast<UINT64>(kJointPaletteStride) * paletteSlot;
        }
    }

    void MeshTraditionalIndirectOwner::RefreshForActivePipeline(
        RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source,
        const MeshRendererTraditionalIndirectHydrationContext& context) {

        if (!HasSourceCommands(source)) {
            AttachToSceneSource(source);
            return;
        }

        for (size_t passIndex = 0;
            passIndex < RENDER3D::GPUDRIVEN::kGpuDrivenPassCount;
            ++passIndex) {

            HydrateStream(streams_[passIndex], context);
            streams_[passIndex].AttachTo(source.passes[passIndex]);
        }
    }

    RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView
    MeshTraditionalIndirectOwner::GetView(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) const {

        const size_t passIndex = RENDER3D::GPUDRIVEN::ToPassIndex(passKind);
        return passIndex < streams_.size()
            ? streams_[passIndex].GetView()
            : RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView{};
    }

} // namespace HIKARI::MESHRENDERER
