#pragma once

#include <array>
#include <d3d12.h>
#include <wrl/client.h>

namespace HIKARI::SHADOW::PIPELINE {

// 深度パス固有の入力形式と面カリングを所有する。材質バケットは呼出側が選ぶ。
class ShadowDepthPipelines final {
public:
    HRESULT Initialize(ID3D12Device* device, ID3D12RootSignature* staticRoot, ID3D12RootSignature* skinnedRoot,
                       D3D12_SHADER_BYTECODE staticVs, D3D12_SHADER_BYTECODE skinnedVs,
                       D3D12_SHADER_BYTECODE pixelShader);
    ID3D12PipelineState* Resolve(bool skinned, bool doubleSided) const noexcept;
    bool IsReady() const noexcept;

private:
    std::array<std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 2>, 2> pipelines_{};
};

} // namespace HIKARI::SHADOW::PIPELINE
