#include "Vfx/Post/HIKARI_SceneCaptureStage.h"

#include <algorithm>
#include <sstream>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Vfx/Post/HIKARI_PostChain.h"
#include "Vfx/Post/HIKARI_PostCommon.h"

namespace HIKARI::POST {

    namespace {
        constexpr size_t SnapshotIndex(
            SceneColorSnapshotKind kind) noexcept {
            return static_cast<size_t>(kind);
        }

        bool IsValidSnapshotKind(
            SceneColorSnapshotKind kind) noexcept {
            return SnapshotIndex(kind) <
                static_cast<size_t>(SceneColorSnapshotKind::Count);
        }

        const char* SnapshotDebugName(
            SceneColorSnapshotKind kind) noexcept {
            return kind == SceneColorSnapshotKind::Opaque
                ? "SceneCapture.ColorOpaque"
                : "SceneCapture.ColorPostOpaque";
        }

        GFX::DESCRIPTOR::SystemSrv SnapshotDescriptor(
            SceneColorSnapshotKind kind,
            uint32_t frameIndex) noexcept {
            return GFX::DESCRIPTOR::SceneColorFrameSlot(
                kind == SceneColorSnapshotKind::PostOpaque,
                frameIndex);
        }
    }

    void SceneCaptureStage::UpdateContext(const GFX::Context& context) {
        context_ = context;
        sceneTarget_.UpdateContext(context);
        for (RenderTarget2D& snapshot : sceneColorSnapshots_) {
            snapshot.UpdateContext(context);
        }
        lightTarget_.UpdateContext(context);
    }

    void SceneCaptureStage::Shutdown() {
        sceneTarget_.Finalize();
        for (RenderTarget2D& snapshot : sceneColorSnapshots_) {
            snapshot.Finalize();
        }
        lightTarget_.Finalize();
        while (!layers_.empty()) layers_.pop();
        sceneColorSrvCpu_.fill({});
        sceneColorSrvGpu_.fill({});
        active_ = false;
        sceneColorReady_.fill(false);
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
        sceneColorReady_.fill(false);
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

    RENDER3D::RenderTextureView SceneCaptureStage::GetCurrentDepthView() const {
        if (!HasCurrentTarget() || !layers_.top().target->HasDepth()) return {};
        const RenderTarget2D& target = *layers_.top().target;
        return {
            target.GetDepthResource(), target.GetDepthSrvGpu(),
            DXGI_FORMAT_R32_FLOAT, static_cast<uint32_t>(target.GetWidth()),
            static_cast<uint32_t>(target.GetHeight()), 1,
            target.GetDepthState(), target.GetDepthResource() != nullptr &&
                target.GetDepthSrvGpu().ptr != 0
        };
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

    bool SceneCaptureStage::EnsureSceneColorSnapshot(
        SceneColorSnapshotKind kind) {
        if (!IsValidSnapshotKind(kind) || !sceneTarget_.GetResource()) {
            return false;
        }
        RenderTarget2D& snapshot =
            sceneColorSnapshots_[SnapshotIndex(kind)];
        const bool invalid = !snapshot.GetResource() ||
            snapshot.GetWidth() != sceneTarget_.GetWidth() ||
            snapshot.GetHeight() != sceneTarget_.GetHeight() ||
            snapshot.GetFormat() != sceneTarget_.GetFormat() ||
            snapshot.HasDepth();
        if (!invalid) return true;
        snapshot.Finalize();
        snapshot.SetDebugName(SnapshotDebugName(kind));
        if (!snapshot.Init(
                sceneTarget_.GetWidth(), sceneTarget_.GetHeight(),
                sceneTarget_.GetFormat(), false, { 0, 0, 0, 1 })) return false;
        return true;
    }

    void SceneCaptureStage::RefreshSceneColorSrv(
        SceneColorSnapshotKind kind) {
        if (!IsValidSnapshotKind(kind)) {
            return;
        }
        const size_t snapshotIndex = SnapshotIndex(kind);
        const uint32_t frameSlot =
            context_.frameIndex % GFX::kFrameResourceCount;
        RenderTarget2D& snapshot = sceneColorSnapshots_[snapshotIndex];
        if (!context_.device || !context_.srvHeap || !snapshot.GetResource()) {
            sceneColorSrvCpu_[snapshotIndex][frameSlot] = {};
            sceneColorSrvGpu_[snapshotIndex][frameSlot] = {};
            sceneColorReady_[snapshotIndex] = false;
            return;
        }
        const UINT size = context_.device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT index = GFX::DESCRIPTOR::ToIndex(
            SnapshotDescriptor(kind, context_.frameIndex));
        sceneColorSrvCpu_[snapshotIndex][frameSlot] =
            GFX::DESCRIPTOR::CpuAt(context_.srvHeap, size, index);
        sceneColorSrvGpu_[snapshotIndex][frameSlot] =
            GFX::DESCRIPTOR::GpuAt(context_.srvHeap, size, index);
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = snapshot.GetFormat();
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MipLevels = 1;
        context_.device->CreateShaderResourceView(
            snapshot.GetResource(),
            &desc,
            sceneColorSrvCpu_[snapshotIndex][frameSlot]);
    }

    RENDER3D::RenderTextureView SceneCaptureStage::CaptureSceneColorSnapshot(
        SceneColorSnapshotKind kind) {
        if (!active_ ||
            !EnsureSceneColorSnapshot(kind) ||
            !context_.cmdList) {
            return {};
        }
        const size_t snapshotIndex = SnapshotIndex(kind);
        RenderTarget2D& snapshot = sceneColorSnapshots_[snapshotIndex];
        RefreshSceneColorSrv(kind);
        const uint32_t frameSlot =
            context_.frameIndex % GFX::kFrameResourceCount;
        if (sceneColorSrvGpu_[snapshotIndex][frameSlot].ptr == 0u) {
            return {};
        }
        sceneTarget_.TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
        snapshot.TransitionColor(D3D12_RESOURCE_STATE_COPY_DEST);
        GFX::PIX::ScopedGpuEvent event(
            context_.cmdList,
            GFX::PIX::kColorPost,
            SnapshotDebugName(kind));
        context_.cmdList->CopyResource(
            snapshot.GetResource(), sceneTarget_.GetResource());
        snapshot.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        sceneTarget_.TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        sceneColorReady_[snapshotIndex] = true;
        return GetSceneColorSnapshotView(kind);
    }

    RENDER3D::RenderTextureView SceneCaptureStage::GetSceneColorSnapshotView(
        SceneColorSnapshotKind kind) const {
        if (!IsValidSnapshotKind(kind)) {
            return {};
        }
        const size_t snapshotIndex = SnapshotIndex(kind);
        const uint32_t frameSlot =
            context_.frameIndex % GFX::kFrameResourceCount;
        const RenderTarget2D& snapshot =
            sceneColorSnapshots_[snapshotIndex];
        return {
            snapshot.GetResource(),
            sceneColorSrvGpu_[snapshotIndex][frameSlot],
            snapshot.GetFormat(),
            static_cast<uint32_t>(snapshot.GetWidth()),
            static_cast<uint32_t>(snapshot.GetHeight()), 1,
            snapshot.GetColorState(), sceneColorReady_[snapshotIndex] &&
                snapshot.GetResource() != nullptr &&
                sceneColorSrvGpu_[snapshotIndex][frameSlot].ptr != 0
        };
    }

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
            << " sceneColorOpaque="
            << sceneColorReady_[SnapshotIndex(
                SceneColorSnapshotKind::Opaque)]
            << " sceneColorPostOpaque="
            << sceneColorReady_[SnapshotIndex(
                SceneColorSnapshotKind::PostOpaque)]
            << " lighting=" << lightingEnabled_
            << " layers=" << layers_.size()
            << "\n  " << sceneTarget_.DumpState()
            << "\n  " << sceneColorSnapshots_[SnapshotIndex(
                SceneColorSnapshotKind::Opaque)].DumpState()
            << "\n  " << sceneColorSnapshots_[SnapshotIndex(
                SceneColorSnapshotKind::PostOpaque)].DumpState()
            << "\n  " << lightTarget_.DumpState();
        return stream.str();
    }

} // namespace HIKARI::POST
