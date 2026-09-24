#pragma once

#include <vector>
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"

namespace HIKARI::MESHRENDERER {

struct MeshViewGpuBindings {
    D3D12_GPU_VIRTUAL_ADDRESS camera = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS cullingCamera = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS light = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS shadow = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS skyEnvironment = 0u;
    RENDER3D::LIGHTING::LocalLightListGpuBindings localLights{};
};

// 主画面・プレビュー・capture が共有する view 単位の CPU staging と GPU 提出。
// 材質、object、joint はそれぞれの所有者に残す。
class MeshViewFrameResources final {
public:
    MeshViewFrameResources() = default;
    ~MeshViewFrameResources();
    MeshViewFrameResources(const MeshViewFrameResources&) = delete;
    MeshViewFrameResources& operator=(const MeshViewFrameResources&) = delete;
    MeshViewFrameResources(MeshViewFrameResources&& other) noexcept;
    MeshViewFrameResources& operator=(MeshViewFrameResources&& other) noexcept;

    bool Initialize(ID3D12Device* device);
    void Finalize();
    bool IsInitialized() const { return uploads_.IsInitialized(); }
    void BuildLocalLights(const RENDER3D::LIGHTING::SceneLightingSet& lighting, const Camera3D& viewCamera,
                          uint32_t width, uint32_t height,
                          std::span<const RENDER3D::LIGHTING::LocalLightShadowAssignment> shadows = {});
    void ClearLocalLights() {
        localLightList = {};
        localLightBuildCached_ = false;
    }
    // 同一 frame slot の提出は上書きしない。slot の再利用は呼び出し側が GPU 完了後に行う。
    bool Publish(const GFX::Context& graphics, const CameraCB* cullingOverride = nullptr);
    bool PublishSkyEnvironment(const GFX::Context& graphics);
    bool PublishLight(const GFX::Context& graphics);
    const MeshViewGpuBindings& Bindings() const { return bindings_; }

    CameraCB camera{};
    CameraCB cullingCamera{};
    LightCB light{};
    ShadowCB shadow{};
    SkyEnvironmentCB skyEnvironment{};
    RENDER3D::LIGHTING::LocalLightListView localLightList{};

private:
    bool PublishConstant(const GFX::Context& graphics, const void* data, size_t bytes,
                         D3D12_GPU_VIRTUAL_ADDRESS& binding);
    DX::DynamicUploadBuffer uploads_{};
    std::vector<RENDER3D::LIGHTING::LocalLightGpuData> lights_{};
    std::vector<RENDER3D::LIGHTING::LocalLightClusterHeader> headers_{};
    std::vector<uint32_t> indices_{};
    MeshViewGpuBindings bindings_{};
    uint64_t localLightBuildSignature_ = 0;
    bool localLightBuildCached_ = false;
};

} // namespace HIKARI::MESHRENDERER
