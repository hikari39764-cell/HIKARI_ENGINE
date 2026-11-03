#include "Vfx/Post/HIKARI_ColorGradingLut.h"
#include "Gfx/HIKARI_UploadTraffic.h"

#include <cstring>
#include <utility>

#include <d3dx12.h>

#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"
#include "Render3D/Color/HIKARI_ColorGradingBake.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::POST {
namespace {
constexpr DXGI_FORMAT kLutFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
} // namespace

void ColorGradingLut::SetSource(const ColorGradingSettings& settings,
                                std::shared_ptr<const ASSETS::COLOR::ColorLutAsset> lut) {
    if (settings_ == settings && lut_ == lut) {
        return;
    }
    settings_ = settings;
    lut_ = std::move(lut);
    dirty_ = true;
}

ID3D12Resource* ColorGradingLut::Prepare(const GFX::Context& context, float gamma) {
    if (context.device != device_ || context.deviceGeneration != deviceGeneration_) {
        texture_.Reset();
        device_ = context.device;
        deviceGeneration_ = context.deviceGeneration;
        dirty_ = true;
    }
    if (dirty_ || gamma != bakedGamma_) {
        active_ = RENDER3D::COLOR::IsColorGradingActive(settings_, lut_.get());
        if (active_) {
            // アップロードに失敗したら次の設定変更まで調整なしで描く（毎フレームの再試行はしない）。
            RENDER3D::COLOR::BakeColorGradingLut(settings_, lut_.get(), gamma, texels_);
            active_ = Upload(context);
        }
        bakedGamma_ = gamma;
        dirty_ = false;
    }
    return GetCurrent();
}

void ColorGradingLut::Release(const GFX::Context& context) {
    if (texture_) {
        GFX::RetireD3D12ObjectForFrame(texture_, context, "ColorGradingLut.Texture");
    }
    device_ = nullptr;
    active_ = false;
    dirty_ = true;
}

bool ColorGradingLut::Upload(const GFX::Context& context) {
    if (context.device == nullptr || context.cmdList == nullptr) {
        return false;
    }
    constexpr UINT size = RENDER3D::COLOR::kColorGradingLutSize;
    const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex3D(kLutFormat, size, size, size, 1u);
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (!texture_) {
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        const HRESULT hr = GFX::VIDEO_MEMORY::CreateCommittedResource(
            context.device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            GFX::VIDEO_MEMORY::Owner::Post, IID_PPV_ARGS(texture_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ColorGradingLut::CreateTexture")) {
            return false;
        }
        texture_->SetName(L"HIKARI.ColorGradingLut");
        state = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rowCount = 0u;
    UINT64 rowBytes = 0u;
    UINT64 totalBytes = 0u;
    context.device->GetCopyableFootprints(&desc, 0u, 1u, 0u, &footprint, &rowCount, &rowBytes, &totalBytes);
    const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    const CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
    Microsoft::WRL::ComPtr<ID3D12Resource> upload{};
    const HRESULT hr = GFX::VIDEO_MEMORY::CreateCommittedResource(
        context.device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        GFX::VIDEO_MEMORY::Owner::Post, IID_PPV_ARGS(&upload));
    uint8_t* mapped = nullptr;
    if (!HIKARI_DX_CHECK(hr, "ColorGradingLut::CreateUpload") ||
        FAILED(upload->Map(0u, nullptr, reinterpret_cast<void**>(&mapped)))) {
        return false;
    }
    const size_t texelRow = static_cast<size_t>(size) * 4u;
    for (UINT z = 0; z < size; ++z) {
        for (UINT y = 0; y < size; ++y) {
            std::memcpy(mapped + footprint.Offset +
                            (static_cast<UINT64>(z) * rowCount + y) * footprint.Footprint.RowPitch,
                        texels_.data() + (static_cast<size_t>(z) * size + y) * texelRow, texelRow * sizeof(uint16_t));
        }
    }
    upload->Unmap(0u, nullptr);
    GFX::UPLOAD_TRAFFIC::Record(GFX::UPLOAD_TRAFFIC::Owner::ColorGrading, totalBytes);

    if (state != D3D12_RESOURCE_STATE_COPY_DEST) {
        const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(texture_.Get(), state, D3D12_RESOURCE_STATE_COPY_DEST);
        context.cmdList->ResourceBarrier(1u, &toCopy);
    }
    const CD3DX12_TEXTURE_COPY_LOCATION destination(texture_.Get(), 0u);
    const CD3DX12_TEXTURE_COPY_LOCATION source(upload.Get(), footprint);
    context.cmdList->CopyTextureRegion(&destination, 0u, 0u, 0u, &source, nullptr);
    const auto toShader = CD3DX12_RESOURCE_BARRIER::Transition(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    context.cmdList->ResourceBarrier(1u, &toShader);
    GFX::RetireD3D12ObjectForFrame(upload, context, "ColorGradingLut.Upload");
    return true;
}

ToneMappingInputs::ToneMappingInputs(const GFX::Context& context, QuadDrawer& quad, RenderTarget2D& source,
                                     ID3D12Resource* gradingLut, ID3D12Resource* sceneDepth)
    : context_(context), views_(RENDER3D::AllocateRenderResourceDescriptorRange(3u)) {
    if (views_.size() != 3u || context.device == nullptr || context.srvHeap == nullptr) {
        for (auto& view : views_) {
            RENDER3D::RetireRenderResourceDescriptor(view, context_, "ToneMappingInputs");
        }
        views_.clear();
        return;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1u;
    view.Format = source.GetFormat();
    context.device->CreateShaderResourceView(source.GetResource(), &view, views_[0].cpu);
    view.Format = DXGI_FORMAT_R32_FLOAT;
    context.device->CreateShaderResourceView(sceneDepth, &view, views_[2].cpu);
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    view.Format = kLutFormat;
    view.Texture3D.MostDetailedMip = 0u;
    view.Texture3D.MipLevels = 1u;
    view.Texture3D.ResourceMinLODClamp = 0.0f;
    context.device->CreateShaderResourceView(gradingLut, &view, views_[1].cpu);
    quad.SetInputTexture(context.srvHeap, views_[0].gpu, views_[1].gpu);
}

ToneMappingInputs::~ToneMappingInputs() {
    for (auto& view : views_) {
        RENDER3D::RetireRenderResourceDescriptor(view, context_, "ToneMappingInputs");
    }
}

} // namespace HIKARI::POST
