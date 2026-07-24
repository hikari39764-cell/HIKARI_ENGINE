#include "Vfx/Post/HIKARI_SceneCaptureStage.h"

#include <algorithm>
#include <sstream>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Vfx/Post/HIKARI_PostChain.h"
#include "Vfx/Post/HIKARI_PostCommon.h"

namespace HIKARI::POST {

    void SceneCaptureStage::UpdateContext(const GFX::Context& context) {
        context_ = context;
        sceneTarget_.UpdateContext(context);
        sceneColorSnapshot_.UpdateContext(context);
        lightTarget_.UpdateContext(context);
    }

    void SceneCaptureStage::Shutdown() {
        sceneTarget_.Finalize();
        sceneColorSnapshot_.Finalize();
        lightTarget_.Finalize();
        while (!layers_.empty()) layers_.pop();
        sceneColorSrvCpu_ = {};
        sceneColorSrvGpu_ = {};
        active_ = false;
        sceneColorReady_ = false;
        lightingEnabled_ = false;
    }

    bool SceneCaptureStage::EnsureSceneTargets(int width, int height) {
        if (width <= 0 || height <= 0) return false;
        const bool sceneInvalid = !sceneTarget_.GetResource() ||
            sceneTarget_.GetWidth() != width || sceneTarget_.GetHeight() != height ||
            sceneTarget_.GetFormat() != DXGI_FORMAT_R16G16B16A16_FLOAT ||
            !sceneTarget_.HasDepth() ||
            !sceneTarget_.AllowsUnorderedAccess();
        if (sceneInvalid) {
            sceneTarget_.Finalize();
            sceneTarget_.SetDebugName("SceneCapture.HDR");
            if (!sceneTarget_.Init(
                    width, height, DXGI_FORMAT_R16G16B16A16_FLOAT,
                    true, { 0, 0, 0, 1 }, true, true)) return false;
        }

        const bool clearChanged =
            lightClearColor_[0] != ambientColor_[0] ||
            lightClearColor_[1] != ambientColor_[1] ||
            lightClearColor_[2] != ambientColor_[2];
        const bool lightInvalid = !lightTarget_.GetResource() ||
            lightTarget_.GetWidth() != width || lightTarget_.GetHeight() != height ||
            lightTarget_.GetFormat() != DXGI_FORMAT_R16G16B16A16_FLOAT ||
            clearChanged;
        if (lightInvalid) {
            lightTarget_.Finalize();
            lightTarget_.SetDebugName("SceneCapture.LightHDR");
            std::copy(std::begin(ambientColor_), std::end(ambientColor_), lightClearColor_);
            if (!lightTarget_.Init(
                    width, height, DXGI_FORMAT_R16G16B16A16_FLOAT,
                    false,
                    { lightClearColor_[0], lightClearColor_[1], lightClearColor_[2], 1 })) {
                return false;
            }
        }
        return true;
    }

    bool SceneCaptureStage::Begin(int width, int height) {
        sceneColorReady_ = false;
        lightingEnabled_ = false;
        while (!layers_.empty()) layers_.pop();
        if (!EnsureSceneTargets(width, height)) {
            active_ = false;
            return false;
        }
        layers_.push({ &sceneTarget_, nullptr });
        sceneTarget_.BeginCapture(0, 0, 0, 1);
        active_ = true;
        return true;
    }

    RenderTarget2D* SceneCaptureStage::End() {
        if (!active_ || layers_.empty() || layers_.top().target == nullptr) return nullptr;
        RenderTarget2D* target = layers_.top().target;
        target->EndCapture();
        layers_.pop();
        active_ = false;
        return target;
    }

    bool SceneCaptureStage::IsActive() const { return active_; }

    bool SceneCaptureStage::HasCurrentTarget() const {
        return active_ && !layers_.empty() && layers_.top().target &&
            layers_.top().target->IsInitialized() &&
            layers_.top().target->GetResource();
    }

    bool SceneCaptureStage::RebindCurrentTarget() {
        if (!HasCurrentTarget()) return false;
        layers_.top().target->Rebind();
        return true;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE SceneCaptureStage::GetCurrentDepthSrv() const {
        if (!HasCurrentTarget() || !layers_.top().target->HasDepth()) return {};
        return layers_.top().target->GetDepthSrvGpu();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SceneCaptureStage::GetCurrentDsv() const {
        return HasCurrentTarget() ? layers_.top().target->GetDsvHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SceneCaptureStage::GetCurrentReadOnlyDsv() const {
        return HasCurrentTarget() ? layers_.top().target->GetReadOnlyDsvHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    bool SceneCaptureStage::BeginCurrentDepthRead() {
        return HasCurrentTarget() && layers_.top().target->BeginDepthRead();
    }

    void SceneCaptureStage::EndCurrentDepthRead() {
        if (HasCurrentTarget()) layers_.top().target->EndDepthRead();
    }

    bool SceneCaptureStage::EnsureSceneColorSnapshot() {
        if (!sceneTarget_.GetResource()) return false;
        const bool invalid = !sceneColorSnapshot_.GetResource() ||
            sceneColorSnapshot_.GetWidth() != sceneTarget_.GetWidth() ||
            sceneColorSnapshot_.GetHeight() != sceneTarget_.GetHeight() ||
            sceneColorSnapshot_.GetFormat() != sceneTarget_.GetFormat() ||
            sceneColorSnapshot_.HasDepth();
        if (!invalid) return true;
        sceneColorSnapshot_.Finalize();
        sceneColorSnapshot_.SetDebugName("SceneCapture.ColorSnapshot");
        if (!sceneColorSnapshot_.Init(
                sceneTarget_.GetWidth(), sceneTarget_.GetHeight(),
                sceneTarget_.GetFormat(), false, { 0, 0, 0, 1 })) return false;
        RefreshSceneColorSrv();
        return true;
    }

    void SceneCaptureStage::RefreshSceneColorSrv() {
        if (!context_.device || !context_.srvHeap || !sceneColorSnapshot_.GetResource()) {
            sceneColorSrvCpu_ = {};
            sceneColorSrvGpu_ = {};
            sceneColorReady_ = false;
            return;
        }
        const UINT size = context_.device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT index = GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::SceneColor);
        sceneColorSrvCpu_ = GFX::DESCRIPTOR::CpuAt(context_.srvHeap, size, index);
        sceneColorSrvGpu_ = GFX::DESCRIPTOR::GpuAt(context_.srvHeap, size, index);
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = sceneColorSnapshot_.GetFormat();
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MipLevels = 1;
        context_.device->CreateShaderResourceView(
            sceneColorSnapshot_.GetResource(), &desc, sceneColorSrvCpu_);
    }

    bool SceneCaptureStage::CaptureSceneColorSnapshot() {
        if (!active_ || !EnsureSceneColorSnapshot() || !context_.cmdList) return false;
        sceneTarget_.TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
        sceneColorSnapshot_.TransitionColor(D3D12_RESOURCE_STATE_COPY_DEST);
        GFX::PIX::ScopedGpuEvent event(
            context_.cmdList, GFX::PIX::kColorPost, "SceneCapture.ColorSnapshot");
        context_.cmdList->CopyResource(
            sceneColorSnapshot_.GetResource(), sceneTarget_.GetResource());
        sceneColorSnapshot_.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        sceneTarget_.TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        sceneColorReady_ = true;
        return true;
    }

    bool SceneCaptureStage::IsSceneColorReady() const {
        return sceneColorReady_ && sceneColorSnapshot_.GetResource() && sceneColorSrvGpu_.ptr;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE SceneCaptureStage::GetSceneColorSrv() const { return sceneColorSrvGpu_; }
    int SceneCaptureStage::GetSceneColorWidth() const { return sceneColorSnapshot_.GetWidth(); }
    int SceneCaptureStage::GetSceneColorHeight() const { return sceneColorSnapshot_.GetHeight(); }

    void SceneCaptureStage::SetAmbientColor(float r, float g, float b) {
        ambientColor_[0] = r; ambientColor_[1] = g; ambientColor_[2] = b;
    }

    void SceneCaptureStage::BeginLightCapture() {
        if (!active_ || !lightTarget_.GetResource()) return;
        if (!layers_.empty()) layers_.top().target->EndCapture();
        lightingEnabled_ = true;
        lightTarget_.BeginCapture(
            ambientColor_[0], ambientColor_[1], ambientColor_[2], 1);
        layers_.push({ &lightTarget_, nullptr });
    }

    void SceneCaptureStage::EndLightCapture() {
        if (!active_ || layers_.empty()) return;
        if (layers_.top().target == &lightTarget_) {
            lightTarget_.EndCapture();
            layers_.pop();
        }
        if (!layers_.empty()) layers_.top().target->Rebind();
    }

    bool SceneCaptureStage::IsLightingEnabled() const { return lightingEnabled_; }
    RenderTarget2D* SceneCaptureStage::GetLightTarget() {
        return lightingEnabled_ ? &lightTarget_ : nullptr;
    }

    void SceneCaptureStage::BeginLayer(
        PostChain& chain, float r, float g, float b, float a) {
        if (!active_ || layers_.empty()) return;
        RenderTarget2D* previous = layers_.top().target;
        chain.UpdateContext(context_);
        chain.PrepareBuffers(previous->GetWidth(), previous->GetHeight());
        RenderTarget2D* layer = chain.GetPing();
        previous->EndCapture();
        layer->BeginCapture(r, g, b, a);
        layers_.push({ layer, &chain });
    }

    void SceneCaptureStage::EndLayer(
        BlendOption blendMode,
        QuadDrawer& quad,
        const CommonParams& commonParams) {
        if (layers_.size() <= 1) return;
        LayerInfo current = layers_.top();
        layers_.pop();
        current.target->EndCapture();
        RenderTarget2D* processed = current.chain && current.chain->HasAny()
            ? current.chain->Execute(*current.target, quad, commonParams)
            : current.target;
        RenderTarget2D* previous = layers_.top().target;
        previous->Rebind();
        if (quad.SetOutputFormat(previous->GetFormat())) {
            quad.DrawBlended(processed->GetSrvHeap(), processed->GetSrvGpu(), blendMode);
        }
    }

    std::string SceneCaptureStage::DumpState() const {
        std::ostringstream stream;
        stream << "[SceneCaptureStage] active=" << active_
            << " sceneColor=" << sceneColorReady_
            << " lighting=" << lightingEnabled_
            << " layers=" << layers_.size()
            << "\n  " << sceneTarget_.DumpState()
            << "\n  " << sceneColorSnapshot_.DumpState()
            << "\n  " << lightTarget_.DumpState();
        return stream.str();
    }

} // namespace HIKARI::POST
