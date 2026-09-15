#include "Render3D/Lighting/HIKARI_DynamicDiffuseGi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3dx12.h>
#include <wrl/client.h>
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::RENDER3D::DYNAMICGI {
    namespace {
        using Microsoft::WRL::ComPtr;
        constexpr auto kReadState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        // SH0 は一様な入射 radiance の約 3.54 倍。FP16 上限で HDR を壊さない。
        constexpr auto kProbeShFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        constexpr uint64_t kProbeBudgetBytes = 32ull*1024*1024;
        struct Constants {
            LIGHTPROBE::IrradianceVolumeGpuData previous{};
            MATH::Vec4 originSpacing{0,0,0,0};
            std::array<uint32_t,4> counts{},work{};
            RAYTRACING::RuntimeRayLightingGpuData lighting{};
        };
        static_assert(sizeof(Constants)==352);
        struct ProbeSet {
            std::array<ComPtr<ID3D12Resource>,10> textures;
            std::vector<RenderResourceView> srvs,uavs;
            TextureResourceHandle visibility;
            LIGHTPROBE::LightProbeVolumeRuntimeData data;
            uint64_t bytes=0;
            void Reset(const GFX::Context& graphics) {
                for(auto& view:srvs) RetireRenderResourceDescriptor(view,graphics,"DynamicGI.SRV");
                for(auto& view:uavs) RetireRenderResourceDescriptor(view,graphics,"DynamicGI.UAV");
                srvs.clear(); uavs.clear();
                if(visibility) ReleaseTextureResource(visibility);
                visibility={};
                for(auto& texture:textures) GFX::RetireD3D12ObjectForFrame(texture,graphics,"DynamicGI.Probe");
                data={}; bytes=0;
            }
            static std::array<D3D12_RESOURCE_DESC,10> Descriptions(UINT x,UINT y,UINT z) {
                std::array<D3D12_RESOURCE_DESC,10> result;
                for(UINT index=0;index<9;++index) result[index]=CD3DX12_RESOURCE_DESC::Tex3D(
                    kProbeShFormat,x,y,static_cast<UINT16>(z),1,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                result[9]=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32_FLOAT,x*y*10,z*10,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                return result;
            }
            bool Create(const GFX::Context& graphics,UINT x,UINT y,UINT z) {
                const auto descriptions=Descriptions(x,y,z);
                for(const auto& desc:descriptions) {
                    const auto allocation=graphics.device->GetResourceAllocationInfo(0,1,&desc);
                    if(allocation.SizeInBytes>kProbeBudgetBytes/2 || bytes>kProbeBudgetBytes/2-allocation.SizeInBytes) return false;
                    bytes+=allocation.SizeInBytes;
                }
                srvs=AllocateRenderResourceDescriptorRange(9);
                uavs=AllocateRenderResourceDescriptorRange(10);
                if(srvs.size()!=9 || uavs.size()!=10) return false;
                const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
                for(UINT index=0;index<10;++index) {
                    const auto& desc=descriptions[index];
                    if(FAILED(graphics.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,kReadState,
                        nullptr,IID_PPV_ARGS(&textures[index])))) return false;
                    textures[index]->SetName(L"HIKARI.DynamicDiffuseGI.ProbeField");
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format=desc.Format;
                    uav.ViewDimension=index==9 ? D3D12_UAV_DIMENSION_TEXTURE2D : D3D12_UAV_DIMENSION_TEXTURE3D;
                    if(index<9) uav.Texture3D.WSize=z;
                    graphics.device->CreateUnorderedAccessView(textures[index].Get(),nullptr,&uav,uavs[index].cpu);
                    if(index<9) {
                        D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=desc.Format;
                        srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D; srv.Texture3D.MipLevels=1;
                        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        graphics.device->CreateShaderResourceView(textures[index].Get(),&srv,srvs[index].cpu);
                    }
                }
                visibility=RegisterTextureResourceFromNative(textures[9].Get(),DXGI_FORMAT_R32G32_FLOAT);
                if(!IsTextureResourceValid(visibility)) return false;
                data.id="DynamicDiffuseGi"; data.enabled=true; data.dynamic=true;
                data.countX=x; data.countY=y; data.countZ=z; data.probeCount=x*y*z;
                data.spacing={1.5f,1.5f,1.5f}; data.size={float(x-1)*1.5f,float(y-1)*1.5f,float(z-1)*1.5f};
                data.blendDistance=3.0f; data.normalBias=0.02f; data.visibilityDistanceScale=60.0f;
                data.visibilityTextureDescriptorIndex=GetTextureResourceSrvDescriptorIndex(visibility);
                return data.visibilityTextureDescriptorIndex<GFX::DESCRIPTOR::kUserSrvCount;
            }
            void Transition(ID3D12GraphicsCommandList* commands,bool toWrite) {
                std::array<D3D12_RESOURCE_BARRIER,10> barriers;
                for(size_t i=0;i<textures.size();++i) barriers[i]=CD3DX12_RESOURCE_BARRIER::Transition(textures[i].Get(),
                    toWrite ? kReadState : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    toWrite ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : kReadState);
                commands->ResourceBarrier(UINT(barriers.size()),barriers.data());
            }
        };
    }

    struct DynamicDiffuseGi::State {
        std::array<ProbeSet,2> sets;
        std::array<DX::DynamicUploadBuffer,GFX::kFrameResourceCount> uploads;
        ComPtr<ID3D12RootSignature> root;
        ComPtr<ID3D12PipelineState> pipeline;
        ID3D12Device* device=nullptr;
        ComPtr<ID3D12Fence> fence;
        uint64_t deviceGeneration=0,lastFence=0,lastFrame=0,sequence=0,retryFrame=0;
        uint64_t sourceLayout=0;
        const void* sourceIdentity=nullptr;
        uint64_t lastRaySceneVersion=0,lastLightingContentHash=0;
        RAYTRACING::RuntimeRayTracingMaterialVersion lastMaterialVersion{};
        UINT published=0,firstProbe=0;
        bool history=false;
        Stats stats;

        bool EnsurePipeline(const GFX::Context& graphics) {
            if(pipeline) return true;
            std::array<CD3DX12_ROOT_PARAMETER,14> parameters;
            parameters[0].InitAsConstantBufferView(0);
            parameters[1].InitAsShaderResourceView(0,8); parameters[2].InitAsShaderResourceView(1,8);
            parameters[3].InitAsShaderResourceView(16); parameters[4].InitAsShaderResourceView(0,6);
            parameters[5].InitAsShaderResourceView(1,6); parameters[6].InitAsShaderResourceView(1,7);
            parameters[7].InitAsShaderResourceView(2,7);
            std::array<CD3DX12_DESCRIPTOR_RANGE,6> ranges;
            ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaCapacity,0,1);
            ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,GFX::DESCRIPTOR::kUserSrvCount,20);
            ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,9,0,2);
            ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,9,0);
            ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,9);
            ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,7);
            for(UINT i=0;i<6;++i) parameters[8+i].InitAsDescriptorTable(1,&ranges[i]);
            std::array<CD3DX12_STATIC_SAMPLER_DESC,2> samplers;
            samplers[0].Init(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
            samplers[1].Init(1,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_WRAP);
            const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(parameters.size()),parameters.data(),2,samplers.data());
            ComPtr<ID3DBlob> signature,errors;
            if(FAILED(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors)) ||
                FAILED(graphics.device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)))) return false;
            namespace SHADER=ASSETS::SHADER;
            SHADER::ShaderAssetContractExpectation contract{};
            contract.engineInterfaceId="HIKARI.DynamicDiffuseGi"; contract.engineInterfaceVersion=1;
            contract.rootLayoutId="HIKARI.DynamicDiffuseGi.Root"; contract.rootLayoutVersion=1;
            if(!HASH::ComputeSha256("DynamicDiffuseGiCB:352;SH9 radiance/confidence/relocation;visibility RG32 oct8+border;shared HCMESH/material query;CloudSolar:64 signed intervals and cumulative T",contract.engineInterfaceHash) ||
                !HASH::ComputeSha256("CBV(b0);AS(t0,s8);Hits(t1,s8);Materials(t16);Programs(t0,t1,s6);Lights(t1,t2,s7);Geometry(t0,s1,1024);Textures(t20);PreviousSH(t0,s2,9);SH(u0,9);Visibility(u9);Sky(t0,s7);Sampler(s0,clamp);Sampler(s1,wrap)",contract.rootLayoutHash)) return false;
            const auto caps=GFX::QueryShaderDeviceCapabilities(graphics.device);
            contract.availableDeviceCaps={caps.shaderModel,caps.rootSignatureVersion,caps.featureMask};
            const auto shader=SHADER::ResolveBuiltinShaderEntry("HIKARI.EngineShader.Render3D_DynamicDiffuseGiCS",
                {"Main",SHADER::ShaderStage::Compute,"cs_6_5"},contract);
            if(!shader) { DEBUGLOG::PushRenderError("Dynamic GI: "+shader.diagnostic); return false; }
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{}; desc.pRootSignature=root.Get();
            const auto bytes=shader.lease.GetDxil(); desc.CS={bytes.data(),bytes.size()};
            if(FAILED(graphics.device->CreateComputePipelineState(&desc,IID_PPV_ARGS(&pipeline)))) return false;
            return true;
        }
    };

    DynamicDiffuseGi::DynamicDiffuseGi():state_(std::make_unique<State>()) {}
    DynamicDiffuseGi::~DynamicDiffuseGi()=default;
    const Stats& DynamicDiffuseGi::GetStats() const { return state_->stats; }
    const char* ToString(Status status) noexcept {
        switch(status) {
        case Status::Off: return "Off";
        case Status::Unsupported: return "DXR / shader format unavailable";
        case Status::PreparingScene: return "Ray scene incomplete or preparing";
        case Status::MissingEnvironment: return "Environment radiance unavailable";
        case Status::ResourceFailure: return "GPU resource or shader unavailable";
        case Status::Ready: return "Ready";
        }
        return "Unknown";
    }

    Frame DynamicDiffuseGi::Prepare(const GFX::Context& graphics,const RAYTRACING::RuntimeRayTracingFrame& rt,
        const MESHRENDERER::MeshMaterialGpuFrameView& materials,const RAYTRACING::RuntimeRayLightingFrame& lighting,
        MATH::Vec3 cameraPosition,DynamicGiQuality quality,uint64_t frameSerial) {
        auto& state=*state_;
        if(state.lastFence!=graphics.currentFrameRetireFenceValue)
            state.stats.updatedProbes=state.stats.primaryRays=0;
        if(quality==DynamicGiQuality::Off) { if(state.device) Reset(graphics); return {}; }
        if(!graphics.device || !graphics.cmdList || !graphics.srvHeap || !graphics.frameFence ||
            !graphics.currentFrameRetireFenceValue || graphics.frameIndex>=GFX::kFrameResourceCount || !materials ||
            !std::isfinite(cameraPosition.x) || !std::isfinite(cameraPosition.y) || !std::isfinite(cameraPosition.z)) {
            state.stats.status=Status::PreparingScene; state.history=false; return {};
        }
        if(state.device && (state.device!=graphics.device || state.deviceGeneration!=graphics.deviceGeneration ||
            state.fence.Get()!=graphics.frameFence)) Reset(graphics);
        if(!state.device) {
            if(!GFX::RAYTRACING::QueryDxrCapabilities(graphics.device).SupportsInlineRayQuery()) {
                state.stats.status=Status::Unsupported; return {};
            }
            for(const auto format:{kProbeShFormat,DXGI_FORMAT_R32G32_FLOAT}) {
                D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
                if(FAILED(graphics.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) ||
                    !(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) || !(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) {
                    state.stats.status=Status::Unsupported; return {};
                }
            }
            state.device=graphics.device; state.deviceGeneration=graphics.deviceGeneration; state.fence=graphics.frameFence;
        }
        if(!lighting.Matches(graphics,rt,{materials.dataVersion,materials.bindingVersion})) {
            state.stats.status=lighting.status==RAYTRACING::RuntimeRayLightingStatus::MissingEnvironment
                ? Status::MissingEnvironment : Status::PreparingScene;
            state.history=false; return {};
        }
        if(state.lastFence==graphics.currentFrameRetireFenceValue) {
            const auto& set=state.sets[state.published];
            if(state.history && state.sourceIdentity==rt.sceneIdentity && state.sourceLayout==rt.sceneLayoutVersion &&
                state.lastRaySceneVersion==rt.version && state.lastMaterialVersion==rt.materialVersion &&
                state.lastLightingContentHash==lighting.contentHash) return {set.data,set.srvs[0].gpu};
            return {};
        }
        const UINT x=quality==DynamicGiQuality::Low ? 12u : quality==DynamicGiQuality::High ? 24u : 16u;
        const UINT y=x/2,z=x,budget=quality==DynamicGiQuality::Low ? 32u : quality==DynamicGiQuality::High ? 128u : 64u;
        if(state.sets[0].data.countX!=x && state.sets[0].bytes!=0) {
            // quality resize は旧 field の fence を待ってから割り当て、世代数で予算を超えない。
            state.history=false;
            if(state.fence->GetCompletedValue()<state.lastFence) { state.stats.status=Status::PreparingScene; return {}; }
            for(auto& set:state.sets) set.Reset(graphics);
        }
        GFX::GPU_PROFILE::ScopedGpuTimer timer(graphics.cmdList,GFX::GPU_PROFILE::Pass::DynamicDiffuseGi);
        state.stats.status=Status::ResourceFailure;
        if(frameSerial<state.retryFrame) return {};
        if(!state.EnsurePipeline(graphics)) { state.retryFrame=frameSerial+60; state.history=false; return {}; }
        if(!state.sets[0].textures[0]) {
            if(!state.sets[0].Create(graphics,x,y,z) || !state.sets[1].Create(graphics,x,y,z)) {
                for(auto& set:state.sets) set.Reset(graphics);
                state.retryFrame=frameSerial+60; state.history=false; return {};
            }
        }
        auto& previous=state.sets[state.published];
        auto& target=state.sets[1-state.published];
        const bool compatible=state.history && state.sourceIdentity==rt.sceneIdentity &&
            state.sourceLayout==rt.sceneLayoutVersion && frameSerial==state.lastFrame+1;
        if(!compatible) state.firstProbe=0;
        constexpr float spacing=1.5f;
        target.data.origin={std::floor(cameraPosition.x/spacing)*spacing-float(x/2)*spacing,
            std::floor(cameraPosition.y/spacing)*spacing-float(y/2)*spacing,
            std::floor(cameraPosition.z/spacing)*spacing-float(z/2)*spacing};
        Constants constants{};
        constants.previous=LIGHTPROBE::BuildIrradianceVolumeGpuData(previous.data,compatible ? 3.0f : 0.0f);
        constants.originSpacing={target.data.origin.x,target.data.origin.y,target.data.origin.z,spacing};
        constants.counts={x,y,z,x*y*z};
        constants.work={UINT(state.sequence),state.firstProbe%constants.counts[3],budget,compatible ? 1u : 0u};
        constants.lighting=lighting.data;
        state.stats.localLights=lighting.data.scene[2]; state.stats.droppedLights=lighting.droppedLights;
        auto& upload=state.uploads[graphics.frameIndex];
        if(!upload.IsInitialized() && !upload.Init(graphics.device,4096)) return {};
        upload.ResetForFrame(graphics.currentFrameRetireFenceValue);
        const auto cb=upload.Allocate(sizeof(constants));
        if(!cb) return {};
        std::memcpy(cb.cpuAddress,&constants,sizeof(constants));
        auto* commands=graphics.cmdList;
        ID3D12DescriptorHeap* heaps[]{graphics.srvHeap}; commands->SetDescriptorHeaps(1,heaps);
        target.Transition(commands,true);
        commands->SetComputeRootSignature(state.root.Get()); commands->SetPipelineState(state.pipeline.Get());
        commands->SetComputeRootConstantBufferView(0,cb.gpuAddress);
        commands->SetComputeRootShaderResourceView(1,rt.accelerationStructure);
        commands->SetComputeRootShaderResourceView(2,rt.hitRecords ? rt.hitRecords : lighting.localLights);
        commands->SetComputeRootShaderResourceView(3,materials.materials);
        commands->SetComputeRootShaderResourceView(4,materials.programParameters);
        commands->SetComputeRootShaderResourceView(5,materials.programResources);
        commands->SetComputeRootShaderResourceView(6,lighting.localLights);
        commands->SetComputeRootShaderResourceView(7,lighting.lightCdf);
        const UINT stride=graphics.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        commands->SetComputeRootDescriptorTable(8,GFX::DESCRIPTOR::GpuAt(graphics.srvHeap,stride,GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaBegin));
        commands->SetComputeRootDescriptorTable(9,GFX::DESCRIPTOR::GpuAt(graphics.srvHeap,stride,GFX::DESCRIPTOR::kUserSrvBegin));
        commands->SetComputeRootDescriptorTable(10,previous.srvs[0].gpu);
        commands->SetComputeRootDescriptorTable(11,target.uavs[0].gpu);
        commands->SetComputeRootDescriptorTable(12,target.uavs[9].gpu);
        commands->SetComputeRootDescriptorTable(13,lighting.sky);
        commands->Dispatch(constants.counts[3],1,1);
        target.Transition(commands,false);
        state.published=1-state.published; state.history=true; target.data.valid=true;
        state.lastFence=graphics.currentFrameRetireFenceValue; state.lastFrame=frameSerial; state.sourceIdentity=rt.sceneIdentity;
        state.sourceLayout=rt.sceneLayoutVersion;
        state.lastRaySceneVersion=rt.version; state.lastMaterialVersion=rt.materialVersion;
        state.lastLightingContentHash=lighting.contentHash;
        ++state.sequence; state.firstProbe=(constants.work[1]+budget)%constants.counts[3];
        state.stats.status=Status::Ready; state.stats.probes=constants.counts[3]; state.stats.updatedProbes=budget;
        state.stats.primaryRays=budget*64; state.stats.sequence=state.sequence; state.stats.probeBytes=previous.bytes+target.bytes;
        return {target.data,target.srvs[0].gpu};
    }
    void DynamicDiffuseGi::Reset(const GFX::Context& graphics) {
        auto& state=*state_;
        for(auto& set:state.sets) set.Reset(graphics);
        for(auto& upload:state.uploads) upload.Finalize(&graphics);
        GFX::RetireD3D12ObjectForFrame(state.pipeline,graphics,"DynamicGI.PSO");
        GFX::RetireD3D12ObjectForFrame(state.root,graphics,"DynamicGI.Root");
        state.device=nullptr; state.fence.Reset(); state.lastFence=0; state.history=false;
        state.sourceIdentity=nullptr; state.sequence=state.retryFrame=0; state.firstProbe=0; state.stats={};
    }
}
