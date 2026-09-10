#include "Vfx/Post/HIKARI_PostPresentationStage.h"

#include <algorithm>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::POST {

    void PostPresentationStage::UpdateContext(const GFX::Context& context) {
        const bool contextChanged =
            context_.device != context.device ||
            context_.srvHeap != context.srvHeap ||
            context_.deviceGeneration != context.deviceGeneration;
        context_ = context;
        editorOutput_.UpdateContext(context);
        if (contextChanged) {
            ResetEditorOutputSrvTracking();
        }
    }

    void PostPresentationStage::Shutdown() {
        editorOutput_.Finalize();
        ResetEditorOutputSrvTracking();
    }

    bool PostPresentationStage::HasCompatibleEditorOutput(
        int width,
        int height) const {
        if (width <= 0 || height <= 0) {
            return false;
        }
        return editorOutput_.GetResource() != nullptr &&
            editorOutput_.GetWidth() == width &&
            editorOutput_.GetHeight() == height &&
            editorOutput_.GetFormat() == DXGI_FORMAT_R8G8B8A8_UNORM &&
            !editorOutput_.HasDepth();
    }

    void PostPresentationStage::ResetEditorOutputSrvTracking() noexcept {
        editorOutputSrvCpu_.fill({});
        editorOutputSrvGpu_.fill({});
        editorOutputSrvResources_.fill(nullptr);
        editorOutputSrvGenerations_.fill(0u);
        editorOutputSrvReady_.fill(false);
        lastPublishedFrameSlot_ = GFX::kFrameResourceCount;
    }

    void PostPresentationStage::PruneObsoleteEditorOutputSrvTracking(
        ID3D12Resource* currentResource) noexcept {

        for (uint32_t frameSlot = 0u;
             frameSlot < GFX::kFrameResourceCount;
             ++frameSlot) {
            if (editorOutputSrvResources_[frameSlot] == currentResource) {
                continue;
            }
            editorOutputSrvCpu_[frameSlot] = {};
            editorOutputSrvGpu_[frameSlot] = {};
            editorOutputSrvResources_[frameSlot] = nullptr;
            editorOutputSrvGenerations_[frameSlot] = 0u;
            editorOutputSrvReady_[frameSlot] = false;
        }
    }

    bool PostPresentationStage::RefreshEditorOutputSrv() {
        if (context_.device == nullptr ||
            context_.srvHeap == nullptr ||
            editorOutput_.GetResource() == nullptr) {
            return false;
        }

        const uint32_t frameSlot =
            context_.frameIndex % GFX::kFrameResourceCount;
        if (editorOutputSrvResources_[frameSlot] ==
                editorOutput_.GetResource() &&
            editorOutputSrvGenerations_[frameSlot] ==
                context_.deviceGeneration &&
            editorOutputSrvCpu_[frameSlot].ptr != 0 &&
            editorOutputSrvGpu_[frameSlot].ptr != 0) {
            return true;
        }

        const UINT descriptorSize =
            context_.device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const GFX::DESCRIPTOR::SystemSrv descriptorSlot =
            GFX::DESCRIPTOR::EditorViewportFrameSlot(context_.frameIndex);
        editorOutputSrvCpu_[frameSlot] = GFX::DESCRIPTOR::CpuAt(
            context_.srvHeap,
            descriptorSize,
            GFX::DESCRIPTOR::ToIndex(descriptorSlot));
        editorOutputSrvGpu_[frameSlot] = GFX::DESCRIPTOR::GpuAt(
            context_.srvHeap,
            descriptorSize,
            GFX::DESCRIPTOR::ToIndex(descriptorSlot));
        if (editorOutputSrvCpu_[frameSlot].ptr == 0u ||
            editorOutputSrvGpu_[frameSlot].ptr == 0u) {
            editorOutputSrvResources_[frameSlot] = nullptr;
            editorOutputSrvGenerations_[frameSlot] = 0u;
            editorOutputSrvReady_[frameSlot] = false;
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        context_.device->CreateShaderResourceView(
            editorOutput_.GetResource(),
            &srv,
            editorOutputSrvCpu_[frameSlot]);
        editorOutputSrvResources_[frameSlot] = editorOutput_.GetResource();
        editorOutputSrvGenerations_[frameSlot] = context_.deviceGeneration;
        return true;
    }

    bool PostPresentationStage::DrawSource(
        RenderTarget2D& source,
        QuadDrawer& quad) const {

        if (source.GetResource() == nullptr ||
            !quad.SetOutputFormat(DXGI_FORMAT_R8G8B8A8_UNORM)) {
            return false;
        }
        quad.DrawFullscreen(source.GetSrvHeap(), source.GetSrvGpu());
        return true;
    }

    bool PostPresentationStage::PresentToEditor(
        RenderTarget2D& source,
        QuadDrawer& quad,
        int expectedWidth,
        int expectedHeight) {

        const bool hasCompatibleOutput = HasCompatibleEditorOutput(
            source.GetWidth(),
            source.GetHeight());
        RenderTarget2D replacement{};
        RenderTarget2D* output = &editorOutput_;
        const bool replacingOutput = !hasCompatibleOutput;
        if (replacingOutput) {
            replacement.UpdateContext(context_);
            replacement.SetDebugName("Presentation.EditorGameView.LDR");
            if (!replacement.Init(
                    source.GetWidth(),
                    source.GetHeight(),
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    false,
                    { 0.0f, 0.0f, 0.0f, 1.0f })) {
                DEBUGLOG::PushRenderError(
                    "[PresentationStage][ERROR] Editor output creation failed.");
                GFX::DumpD3D12InfoQueue(
                    context_.device,
                    "Presentation editor output creation failed");
                BindBackBufferFullViewport();
                return false;
            }
            output = &replacement;
        }

        output->BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
        const bool drew = DrawSource(source, quad);
        output->EndCapture();
        BindBackBufferFullViewport();
        const bool sizeCurrent =
            expectedWidth <= 0 ||
            expectedHeight <= 0 ||
            (output->GetWidth() == expectedWidth &&
                output->GetHeight() == expectedHeight);
        if (!drew || !sizeCurrent) {
            return false;
        }

        if (replacingOutput) {
            editorOutput_.Swap(replacement);
        }
        const bool srvReady = RefreshEditorOutputSrv();
        if (!srvReady) {
            if (replacingOutput) {
                editorOutput_.Swap(replacement);
            }
            BindBackBufferFullViewport();
            return false;
        }

        const uint32_t frameSlot =
            context_.frameIndex % GFX::kFrameResourceCount;
        editorOutputSrvReady_[frameSlot] = true;
        lastPublishedFrameSlot_ = frameSlot;
        if (replacingOutput) {
            PruneObsoleteEditorOutputSrvTracking(editorOutput_.GetResource());
        }
        return true;
    }

    bool PostPresentationStage::PresentToBackBuffer(
        RenderTarget2D& source,
        QuadDrawer& quad) {

        if (context_.cmdList == nullptr) {
            return false;
        }
        BindBackBufferFullViewport();
        return DrawSource(source, quad);
    }

    void PostPresentationStage::BindBackBufferFullViewport() const {
        if (context_.cmdList == nullptr) {
            return;
        }
        context_.cmdList->OMSetRenderTargets(1, &context_.rtv, FALSE, nullptr);
        const float width =
            static_cast<float>((std::max)(context_.backBufferWidth, 1));
        const float height =
            static_cast<float>((std::max)(context_.backBufferHeight, 1));
        const D3D12_VIEWPORT viewport{
            0.0f, 0.0f, width, height, 0.0f, 1.0f
        };
        const D3D12_RECT scissor{
            0, 0, static_cast<LONG>(width), static_cast<LONG>(height)
        };
        context_.cmdList->RSSetViewports(1, &viewport);
        context_.cmdList->RSSetScissorRects(1, &scissor);
    }

    bool PostPresentationStage::IsEditorOutputReady(
        int expectedWidth,
        int expectedHeight) const {

        const uint32_t frameSlot =
            context_.frameIndex % GFX::kFrameResourceCount;
        const bool currentSize =
            expectedWidth <= 0 ||
            expectedHeight <= 0 ||
            (editorOutput_.GetWidth() == expectedWidth &&
                editorOutput_.GetHeight() == expectedHeight);
        const bool currentReady =
            editorOutputSrvReady_[frameSlot] &&
            editorOutputSrvGpu_[frameSlot].ptr != 0 &&
            editorOutputSrvResources_[frameSlot] != nullptr &&
            currentSize;
        if (currentReady) {
            return true;
        }

        if (lastPublishedFrameSlot_ >= GFX::kFrameResourceCount) {
            return false;
        }
        return editorOutputSrvReady_[lastPublishedFrameSlot_] &&
            editorOutputSrvGpu_[lastPublishedFrameSlot_].ptr != 0 &&
            editorOutputSrvResources_[lastPublishedFrameSlot_] != nullptr;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE
    PostPresentationStage::GetEditorOutputSrv() const {
        const uint32_t frameSlot =
            context_.frameIndex % GFX::kFrameResourceCount;
        if (editorOutputSrvReady_[frameSlot] &&
            editorOutputSrvGpu_[frameSlot].ptr != 0) {
            return editorOutputSrvGpu_[frameSlot];
        }
        if (lastPublishedFrameSlot_ < GFX::kFrameResourceCount) {
            return editorOutputSrvGpu_[lastPublishedFrameSlot_];
        }
        return {};
    }

    int PostPresentationStage::GetEditorOutputWidth() const {
        return editorOutput_.GetWidth();
    }

    int PostPresentationStage::GetEditorOutputHeight() const {
        return editorOutput_.GetHeight();
    }

} // namespace HIKARI::POST
