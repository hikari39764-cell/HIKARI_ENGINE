#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Assets/Shader/HIKARI_PostEffectShaderContract.h"
#include "Core/HIKARI_TimeService.h"
#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <cstring>
#include <d3dx12.h>
#include <limits>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"

namespace HIKARI {
namespace POST {

GFX::Context PostEffect::context_{};

PostEffect::PostEffect() {
    ZeroMemory(&params_, sizeof(params_));
    constantFrameSerials_.fill((std::numeric_limits<uint64_t>::max)());
    constantDrawCursors_.fill(0u);
}

void PostEffect::UpdateContext(const GFX::Context& ctx) { context_ = ctx; }

PostEffect::~PostEffect() { ReleaseConstantBuffer(); }

bool BuildPostEffectPasses(const PostEffectDefinition& definition, const AssetRegistry* registry,
                           std::vector<std::unique_ptr<PostEffect>>& passes) {
    std::vector<std::unique_ptr<PostEffect>> prepared;
    prepared.reserve(definition.passes.size());
    for (const auto& pass : definition.passes) {
        auto instance = std::make_unique<PostEffect>();
        if (!instance->LoadPixelShader(pass, registry, definition.requiresSurfaceFeatureMask))
            return false;
        prepared.push_back(std::move(instance));
    }
    passes = std::move(prepared);
    return !passes.empty();
}

void PostEffect::ReleaseConstantBuffer() noexcept {
    if (constantBuffer_ && mappedPtr_) {
        constantBuffer_->Unmap(0, nullptr);
    }
    mappedPtr_ = nullptr;
    GFX::RetireD3D12ObjectForFrame(constantBuffer_, constantBufferContext_, "PostEffect.ConstantBuffer");
    constantBufferContext_ = {};
    constantStride_ = 0u;
    constantFrameSerials_.fill((std::numeric_limits<uint64_t>::max)());
    constantDrawCursors_.fill(0u);
}

bool PostEffect::CreateConstantBuffer() {
    const bool sameDevice = constantBuffer_ && mappedPtr_ && constantBufferContext_.device == context_.device &&
                            constantBufferContext_.deviceGeneration != 0u &&
                            constantBufferContext_.deviceGeneration == context_.deviceGeneration;
    if (sameDevice) {
        return true;
    }
    if (constantBuffer_ || mappedPtr_ != nullptr) {
        ReleaseConstantBuffer();
    }

    auto* device = context_.device;
    if (!device) {
        DEBUGLOG::PushRenderError("[PostEffect][ERROR] context_.device is null; postpone constant buffer creation.");
        return false;
    }

    constantStride_ = Align256(sizeof(CommonParams));
    const UINT64 size = static_cast<UINT64>(constantStride_) * kConstantDrawCapacityPerFrame * GFX::kFrameResourceCount;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    HRESULT hr =
        device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                        nullptr, IID_PPV_ARGS(constantBuffer_.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "PostEffect::CreateConstantBuffer")) {
        return false;
    }

    void* mapped = nullptr;
    hr = constantBuffer_->Map(0, nullptr, &mapped);
    if (!HIKARI_DX_CHECK(hr, "PostEffect::Map constant buffer")) {
        constantBuffer_.Reset();
        mappedPtr_ = nullptr;
        constantStride_ = 0u;
        return false;
    }
    mappedPtr_ = static_cast<uint8_t*>(mapped);
    constantBufferContext_ = context_;
    constantFrameSerials_.fill((std::numeric_limits<uint64_t>::max)());
    constantDrawCursors_.fill(0u);
    return true;
}
bool PostEffect::LoadPixelShader(const PostEffectPass& pass, const AssetRegistry* registry, bool surfaceFeatureMask) {
    const bool projectShader = !pass.shaderAssetGuid.value.empty();
    if (context_.device == nullptr || pass.entryId.empty() || pass.programId.empty() == !projectShader ||
        (projectShader && (registry == nullptr || !IsValidAssetGuid(pass.shaderAssetGuid.value)))) {
        DEBUGLOG::PushRenderError("[PostEffect][ERROR] Shader request is invalid.");
        return false;
    }
    const GFX::ShaderDeviceCapabilities capabilities = GFX::QueryShaderDeviceCapabilities(context_.device);
    const ASSETS::SHADER::ShaderAssetContractExpectation contract = ASSETS::SHADER::MakePostEffectShaderContract(
        {capabilities.shaderModel, capabilities.rootSignatureVersion, capabilities.featureMask});
    const ASSETS::SHADER::ShaderAssetEntryExpectation entry{pass.entryId, ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0"};
    auto resolved = projectShader ? ASSETS::SHADER::ShaderAssetResolver::ResolveByGuid(*registry, pass.shaderAssetGuid,
                                                                                       entry, contract)
                                  : ASSETS::SHADER::ResolveBuiltinShaderEntry(pass.programId, entry, contract);
    std::string diagnostic{};
    const ASSETS::SHADER::ShaderCookedArtifact* artifact = resolved ? resolved.lease.GetArtifact() : nullptr;
    if (!resolved || artifact == nullptr || !ASSETS::SHADER::ValidatePostEffectShaderArtifact(*artifact, diagnostic)) {
        DEBUGLOG::PushRenderError("[PostEffect][ERROR] HSHADERC resolve failed: " +
                                  (diagnostic.empty() ? resolved.diagnostic : diagnostic));
        return false;
    }
    // Global project effects may consume only the inputs declared by their owner.
    // Internal built-in passes retain their specialized t1/t2 binding owners.
    if (projectShader) {
        for (const auto& binding : resolved.lease.GetEntry()->bindings) {
            if (binding.kind == ASSETS::SHADER::ShaderResourceBindingKind::ShaderResource &&
                (binding.registerIndex == 1u || (binding.registerIndex == 2u && !surfaceFeatureMask))) {
                DEBUGLOG::PushRenderError("[PostEffect][ERROR] Project shader uses an undeclared surface input.");
                return false;
            }
        }
    }
    psLease_ = std::move(resolved.lease);
    return true;
}

void PostEffect::ApplyCommonParams(const CommonParams& p) { params_ = p; }

void PostEffect::SetTime(float t) { params_.time = t; }

bool PostEffect::BindAndDraw(QuadDrawer& drawer, bool additive) {
    const std::span<const uint8_t> cooked = psLease_.GetDxil();
    if (cooked.empty()) {
        return false;
    }
    if (!CreateConstantBuffer()) {
        return false;
    }
    if (!constantBuffer_) {
        return false;
    }
    constantBufferContext_ = context_;

    const uint32_t frameSlot = context_.frameIndex % GFX::kFrameResourceCount;
    const uint64_t frameSerial = TIME::GetFrameContext().frameIndex;
    if (constantFrameSerials_[frameSlot] != frameSerial) {
        constantFrameSerials_[frameSlot] = frameSerial;
        constantDrawCursors_[frameSlot] = 0u;
    }

    const uint32_t drawSlot = constantDrawCursors_[frameSlot];
    if (drawSlot >= kConstantDrawCapacityPerFrame) {
        DEBUGLOG::PushRenderError("[PostEffect][ERROR] Per-frame constant draw capacity exhausted; "
                                  "the draw was skipped to preserve GPU resource lifetime.");
        return false;
    }
    ++constantDrawCursors_[frameSlot];

    const UINT64 allocationIndex = static_cast<UINT64>(frameSlot) * kConstantDrawCapacityPerFrame + drawSlot;
    const UINT64 byteOffset = allocationIndex * constantStride_;
    memcpy(mappedPtr_ + byteOffset, &params_, sizeof(params_));

    if (!drawer.SetPixelShader(cooked, additive)) {
        return false;
    }
    drawer.SetConstantBuffer(constantBuffer_->GetGPUVirtualAddress() + byteOffset);
    drawer.DrawFullscreen();
    return true;
}

} // namespace POST
} // namespace HIKARI
