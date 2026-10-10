#include "Render3D/RayTracing/HIKARI_RuntimeRayQueryBindings.h"

#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI::RENDER3D::RAYTRACING {

void InitRuntimeRayQueryRootParameters(std::span<CD3DX12_ROOT_PARAMETER> parameters,
                                       std::span<CD3DX12_DESCRIPTOR_RANGE> ranges, UINT skyParameter) {
    parameters[0].InitAsConstantBufferView(0);
    parameters[1].InitAsShaderResourceView(0, 8);
    parameters[2].InitAsShaderResourceView(1, 8);
    parameters[3].InitAsShaderResourceView(16);
    parameters[4].InitAsShaderResourceView(0, 6);
    parameters[5].InitAsShaderResourceView(1, 6);
    parameters[6].InitAsShaderResourceView(1, 7);
    parameters[7].InitAsShaderResourceView(2, 7);
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaCapacity, 0, 1);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GFX::DESCRIPTOR::kUserSrvCount, 20);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 7);
    parameters[8].InitAsDescriptorTable(1, &ranges[0]);
    parameters[9].InitAsDescriptorTable(1, &ranges[1]);
    parameters[10].InitAsShaderResourceView(2, 8);
    parameters[skyParameter].InitAsDescriptorTable(1, &ranges[2]);
}

std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> RuntimeRayQueryStaticSamplers() {
    std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> samplers;
    samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                     D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    samplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                     D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    return samplers;
}

void BindRuntimeRayQueryRoots(ID3D12GraphicsCommandList* commands, const GFX::Context& graphics,
                              const RuntimeRayLightingFrame& lighting,
                              const MESHRENDERER::MeshMaterialGpuFrameView& materials, UINT skyParameter) {
    const RuntimeRayTracingFrame& scene = lighting.scene;
    commands->SetComputeRootShaderResourceView(1, scene.accelerationStructure);
    // 命中記録が無い空の場面でも有効な番地を束ねる（shader は scene.w で空を判定する）。
    commands->SetComputeRootShaderResourceView(2, scene.hitRecords ? scene.hitRecords : lighting.localLights);
    commands->SetComputeRootShaderResourceView(3, materials.materials);
    commands->SetComputeRootShaderResourceView(4, materials.programParameters);
    commands->SetComputeRootShaderResourceView(5, materials.programResources);
    commands->SetComputeRootShaderResourceView(6, lighting.localLights);
    commands->SetComputeRootShaderResourceView(7, lighting.lightCdf);
    const UINT stride = graphics.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    commands->SetComputeRootDescriptorTable(
        8, GFX::DESCRIPTOR::GpuAt(graphics.srvHeap, stride, GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaBegin));
    commands->SetComputeRootDescriptorTable(
        9, GFX::DESCRIPTOR::GpuAt(graphics.srvHeap, stride, GFX::DESCRIPTOR::kUserSrvBegin));
    // 変形した頂点が無い場面でも有効な番地を束ねる（命中記録が変形を指さなければ読まれない）。
    commands->SetComputeRootShaderResourceView(10, scene.deformedVertices
                                                       ? scene.deformedVertices
                                                       : (scene.hitRecords ? scene.hitRecords : lighting.localLights));
    commands->SetComputeRootDescriptorTable(skyParameter, lighting.sky);
}

} // namespace HIKARI::RENDER3D::RAYTRACING
