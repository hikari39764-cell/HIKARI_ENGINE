#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_OwnedTraditionalIndirectStream.h"

namespace HIKARI {
namespace DX {
class DynamicUploadBuffer;
}
namespace GFX {
struct Context;
}
} // namespace HIKARI

namespace HIKARI::MESHRENDERER {

class MeshPrimitiveCache;
struct MeshRendererDebugStats;

struct MeshRendererTraditionalIndirectHydrationContext {
    ID3D12Device* device = nullptr;
    MeshPrimitiveCache* primitiveCache = nullptr;
    MeshRendererDebugStats* debugStats = nullptr;
    const GFX::Context* graphics = nullptr;
    DX::DynamicUploadBuffer* deformationUploads = nullptr;
};

// GpuDrivenSceneSource の traditionalIndirect は参照 view なので、
// MeshRenderer がフレームを跨いで使う command/record/palette の寿命をここで所有する。
// Mesh の GPU view 復元と joint palette の水合も、この sidecar 境界に閉じ込める。
class MeshTraditionalIndirectOwner {
public:
    void Clear();

    void CopyFromSceneSource(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source);
    void AttachToSceneSource(RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) const;
    bool RefreshForActivePipeline(RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source,
                                  const MeshRendererTraditionalIndirectHydrationContext& context);

    bool HasSourceCommands(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) const;
    RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView GetView(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) const;

private:
    using OwnedStream = RENDER3D::GPUDRIVEN::OwnedTraditionalIndirectStream;

    bool HydrateStream(OwnedStream& stream, const MeshRendererTraditionalIndirectHydrationContext& context);

    std::array<OwnedStream, RENDER3D::GPUDRIVEN::kGpuDrivenPassCount> streams_{};
};

} // namespace HIKARI::MESHRENDERER
