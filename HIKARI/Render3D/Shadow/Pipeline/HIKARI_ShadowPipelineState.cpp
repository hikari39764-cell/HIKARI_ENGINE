#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <iterator>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "HIKARI_Services.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Shadow/Pipeline/HIKARI_ShadowRootParameters.h"

namespace HIKARI::SHADOW::INTERNAL {

    namespace {

        D3D12_DESCRIPTOR_RANGE MakeSrvRange(
            UINT descriptorCount,
            UINT shaderRegister,
            UINT registerSpace = 0) {

            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = descriptorCount;
            range.BaseShaderRegister = shaderRegister;
            range.RegisterSpace = registerSpace;
            range.OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            return range;
        }

        D3D12_ROOT_PARAMETER MakeRootDescriptor(
            D3D12_ROOT_PARAMETER_TYPE type,
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) {

            D3D12_ROOT_PARAMETER parameter{};
            parameter.ParameterType = type;
            parameter.ShaderVisibility = visibility;
            parameter.Descriptor.ShaderRegister = shaderRegister;
            parameter.Descriptor.RegisterSpace = registerSpace;
            return parameter;
        }

        D3D12_ROOT_PARAMETER MakeRootDescriptorTable(
            const D3D12_DESCRIPTOR_RANGE& range,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) {

            D3D12_ROOT_PARAMETER parameter{};
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.ShaderVisibility = visibility;
            parameter.DescriptorTable.NumDescriptorRanges = 1;
            parameter.DescriptorTable.pDescriptorRanges = &range;
            return parameter;
        }

        D3D12_ROOT_PARAMETER MakeRootConstants(
            UINT shaderRegister,
            UINT valueCount,
            UINT registerSpace = 0) {

            D3D12_ROOT_PARAMETER parameter{};
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            parameter.Constants.ShaderRegister = shaderRegister;
            parameter.Constants.RegisterSpace = registerSpace;
            parameter.Constants.Num32BitValues = valueCount;
            return parameter;
        }

        bool CreateNamedRootSignature(
            ID3D12Device* device,
            const D3D12_ROOT_SIGNATURE_DESC& description,
            ComPtr<ID3D12RootSignature>& rootSignature,
            const char* operation,
            const wchar_t* debugName) {

            ComPtr<ID3DBlob> signatureBlob;
            ComPtr<ID3DBlob> errorBlob;
            const HRESULT serializeResult = D3D12SerializeRootSignature(
                &description,
                D3D_ROOT_SIGNATURE_VERSION_1,
                signatureBlob.GetAddressOf(),
                errorBlob.GetAddressOf());
            if (FAILED(serializeResult)) {
                if (errorBlob != nullptr) {
                    OutputDebugStringA(
                        static_cast<const char*>(errorBlob->GetBufferPointer()));
                }
                return false;
            }

            const HRESULT createResult = device->CreateRootSignature(
                0,
                signatureBlob->GetBufferPointer(),
                signatureBlob->GetBufferSize(),
                IID_PPV_ARGS(rootSignature.ReleaseAndGetAddressOf()));
            if (!HIKARI_DX_CHECK(createResult, operation)) {
                return false;
            }
            GFX::SetD3D12Name(rootSignature.Get(), debugName);
            return true;
        }

    } // namespace

    bool CreateShadowPipelineState(ID3D12Device* device) {
        if (!GFX::SupportsShaderModel6(device)) {
            DEBUGLOG::PushRenderError("[Shadow][ERROR] Shader Model 6.0 is not supported by this device.");
            return false;
        }

        ComPtr<ID3DBlob> staticVs;
        ComPtr<ID3DBlob> skinnedVs;
        ComPtr<ID3DBlob> ps;
        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_ShadowStaticVS.hlsl", "main", GFX::ShaderStage::Vertex, staticVs.GetAddressOf())) {
            DEBUGLOG::PushRenderError("[Shadow][ERROR] Compile ShadowStaticVS failed.");
            return false;
        }
        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_ShadowSkinnedVS.hlsl", "main", GFX::ShaderStage::Vertex, skinnedVs.GetAddressOf())) {
            DEBUGLOG::PushRenderError("[Shadow][ERROR] Compile ShadowSkinnedVS failed.");
            return false;
        }
        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_ShadowAlphaPS.hlsl", "main", GFX::ShaderStage::Pixel, ps.GetAddressOf())) {
            DEBUGLOG::PushRenderError("[Shadow][ERROR] Compile ShadowAlphaPS failed.");
            return false;
        }

        const D3D12_DESCRIPTOR_RANGE textureRange = MakeSrvRange(1, 0);
        const D3D12_DESCRIPTOR_RANGE materialDataRange = MakeSrvRange(1, 16);
        const D3D12_DESCRIPTOR_RANGE surfaceGpuSceneRange = MakeSrvRange(1, 17);
        const D3D12_DESCRIPTOR_RANGE objectDataRange = MakeSrvRange(1, 15);
        const D3D12_DESCRIPTOR_RANGE texturePoolRange =
            MakeSrvRange(GFX::DESCRIPTOR::kUserSrvCount, 20);
        const D3D12_DESCRIPTOR_RANGE clusterGeometryPoolRange =
            MakeSrvRange(GFX::DESCRIPTOR::kSystemSrvDynamicCount, 0, 1);

        D3D12_ROOT_PARAMETER params[PIPELINE::kShadowStaticRootParamDeformationPalettes + 1]{};
        params[PIPELINE::kShadowStaticRootParamCamera] =
            MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 0);
        params[PIPELINE::kShadowStaticRootParamObject] =
            MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 1);
        params[PIPELINE::kShadowStaticRootParamBaseColorTexture] =
            MakeRootDescriptorTable(
                textureRange,
                D3D12_SHADER_VISIBILITY_PIXEL);
        params[PIPELINE::kShadowStaticRootParamMaterialData] =
            MakeRootDescriptorTable(materialDataRange);
        params[PIPELINE::kShadowStaticRootParamSurfaceGpuScene] =
            MakeRootDescriptorTable(surfaceGpuSceneRange);
        params[PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl] =
            MakeRootConstants(
                8,
                RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount);
        params[PIPELINE::kShadowStaticRootParamTexturePool] =
            MakeRootDescriptorTable(
                texturePoolRange,
                D3D12_SHADER_VISIBILITY_PIXEL);
        params[PIPELINE::kShadowStaticRootParamMaterialIndex] =
            MakeRootConstants(7, 1);
        params[PIPELINE::kShadowStaticRootParamObjectData] =
            MakeRootDescriptorTable(objectDataRange);
        params[PIPELINE::kShadowStaticRootParamClusterGeometryPool] =
            MakeRootDescriptorTable(clusterGeometryPoolRange);
        params[PIPELINE::kShadowStaticRootParamMeshletVisibleRanges] =
            MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, 18);
        params[PIPELINE::kShadowStaticRootParamMeshletVisibleClusterList] =
            MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, 19);
        params[PIPELINE::kShadowStaticRootParamCullingCamera] =
            MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 9);
        params[PIPELINE::kShadowStaticRootParamDeformationPalettes] =
            MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, 0, 3);
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = static_cast<UINT>(std::size(params));
        rsDesc.pParameters = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers = &sampler;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        if (!CreateNamedRootSignature(
            device,
            rsDesc,
            gShadowRendererState.rootSig,
            "CreateRootSignature: Shadow static",
            L"Shadow Static RootSignature")) {
            return false;
        }

        D3D12_ROOT_PARAMETER skinnedParams[PIPELINE::kShadowSkinnedRootParamJointPalette + 1]{};
        for (size_t i = 0; i < std::size(params); ++i) {
            skinnedParams[i] = params[i];
        }
        skinnedParams[PIPELINE::kShadowSkinnedRootParamJointPalette] =
            MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 3);

        D3D12_ROOT_SIGNATURE_DESC skinnedRsDesc = rsDesc;
        skinnedRsDesc.NumParameters = static_cast<UINT>(std::size(skinnedParams));
        skinnedRsDesc.pParameters = skinnedParams;
        if (!CreateNamedRootSignature(
            device,
            skinnedRsDesc,
            gShadowRendererState.skinnedRootSig,
            "CreateRootSignature: Shadow skinned",
            L"Shadow Skinned RootSignature")) {
            return false;
        }

        const D3D12_INPUT_ELEMENT_DESC staticInput[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, u)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, uv1)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        const D3D12_INPUT_ELEMENT_DESC skinnedInput[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv0)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv1)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, color0)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "JOINTS", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = gShadowRendererState.rootSig.Get();
        psoDesc.VS = { staticVs->GetBufferPointer(), staticVs->GetBufferSize() };
        psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.InputLayout = { staticInput, static_cast<UINT>(std::size(staticInput)) };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 0;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        HRESULT hr = device->CreateGraphicsPipelineState(
            &psoDesc,
            IID_PPV_ARGS(gShadowRendererState.staticPso.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "Create PSO: Shadow static")) {
            return false;
        }
        GFX::SetD3D12Name(gShadowRendererState.staticPso.Get(), L"Shadow Static PSO");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedPsoDesc = psoDesc;
        skinnedPsoDesc.pRootSignature = gShadowRendererState.skinnedRootSig.Get();
        skinnedPsoDesc.VS = { skinnedVs->GetBufferPointer(), skinnedVs->GetBufferSize() };
        skinnedPsoDesc.InputLayout = { skinnedInput, static_cast<UINT>(std::size(skinnedInput)) };
        hr = device->CreateGraphicsPipelineState(&skinnedPsoDesc, IID_PPV_ARGS(gShadowRendererState.skinnedPso.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "Create PSO: Shadow skinned")) {
            return false;
        }
        GFX::SetD3D12Name(gShadowRendererState.skinnedPso.Get(), L"Shadow Skinned PSO");
        if (!gShadowRendererState.traditionalCommandStreamBuffer.Initialize(
            device,
            gShadowRendererState.rootSig.Get(),
            PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl,
            RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount)) {
            DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow indirect draw buffer initialization failed. Direct shadow record path will be used.");
        } else if (!gShadowRendererState.traditionalCommandStreamBuffer.InitializeSkinnedCommandStream(
            device,
            gShadowRendererState.skinnedRootSig.Get(),
            PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl,
            PIPELINE::kShadowSkinnedRootParamJointPalette)) {
            DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow skinned indirect command signature initialization failed. Skinned GPU-driven shadow stream will be unavailable.");
        }
        if (!gShadowRendererState.clusterGpuCullingPass.Initialize(
            device,
            gShadowRendererState.rootSig.Get(),
            PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl,
            RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount)) {
            DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow cluster GPU culling initialization failed. GPU-driven shadow pass will be unavailable.");
        }
        if (!gShadowRendererState.meshletRenderBackend.Initialize(
            device,
            gShadowRendererState.rootSig.Get(),
            RENDER3D::MESHLET::MeshletPipelineMask::ShadowRenderer)) {
            DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow meshlet backend initialization failed. GPU-driven shadow mesh shader route will be unavailable.");
        }
        gShadowRendererState.clusterGpuDrivenProducer.Attach(&gShadowRendererState.clusterGpuCullingPass);
        gShadowRendererState.gpuDrivenLayer.Attach(
            &gShadowRendererState.surfaceGpuSceneBuffer,
            &gShadowRendererState.traditionalCommandStreamBuffer,
            &gShadowRendererState.clusterGpuDrivenProducer);
        if (!gShadowRendererState.gpuDrivenLayer.Initialize(
            device,
            gShadowRendererState.rootSig.Get(),
            PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl,
            RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount)) {
            DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow GPU-driven layer initialization failed. Shadow draw backend will be unavailable.");
        }
        return true;
    }

    bool EnsureShadowRendererInitialized() {
        if (gShadowRendererState.initialized) {
            return true;
        }
        auto* device = SERVICES::gCtx.device;
        if (device == nullptr) {
            return false;
        }
        gShadowRendererState.fallbackTextureResource = RENDER3D::LoadTextureResource(
            "shadow/fallback_white",
            "HIKARI/white1x1.png");
        gShadowRendererState.fallbackTextureHandle =
            RENDER3D::GetTextureResourceBackendHandle(gShadowRendererState.fallbackTextureResource);
        gShadowRendererState.initialized =
            RENDER3D::IsTextureResourceValid(gShadowRendererState.fallbackTextureResource) &&
            CreateShadowFrameResources(device) &&
            CreateShadowPipelineState(device);
        return gShadowRendererState.initialized;
    }

} // namespace HIKARI::SHADOW::INTERNAL
