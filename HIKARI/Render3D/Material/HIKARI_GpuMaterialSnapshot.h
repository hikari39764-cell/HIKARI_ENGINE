#pragma once

#include <span>
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Render3D/Material/HIKARI_MaterialSnapshot.h"
#include "Render3D/Resources/HIKARI_TextureResourceSnapshot.h"

namespace HIKARI::RENDER3D::MATERIAL {

// ジョブ専用の不変な材質テーブル。TextureResourceSnapshot の view index を消費する。
// Reset / 破棄は通常の frame fence に GPU resource の解放を委譲する。
class GpuMaterialSnapshot final {
public:
    ~GpuMaterialSnapshot();
    bool Initialize(ID3D12Device* device, std::span<const MaterialSnapshot> materials,
                    const TextureResourceSnapshot& textures, std::string& error);
    void Reset();
    D3D12_GPU_VIRTUAL_ADDRESS GetMaterialAddress() const { return materialAddress_; }
    D3D12_GPU_VIRTUAL_ADDRESS GetParameterAddress() const { return parameterAddress_; }
    D3D12_GPU_VIRTUAL_ADDRESS GetResourceAddress() const { return resourceAddress_; }

private:
    DX::DynamicUploadBuffer upload_{};
    D3D12_GPU_VIRTUAL_ADDRESS materialAddress_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS parameterAddress_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS resourceAddress_ = 0;
};

} // namespace HIKARI::RENDER3D::MATERIAL
