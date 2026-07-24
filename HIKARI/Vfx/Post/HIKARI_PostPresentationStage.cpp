#include "Vfx/Post/HIKARI_PostPresentationStage.h"

#include <algorithm>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "HIKARI_Core.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::POST {

    namespace {
        struct LetterboxRect {
            float x = 0.0f;
            float y = 0.0f;
            float width = 1.0f;
            float height = 1.0f;
        };

        LetterboxRect ComputeLetterboxRect(int width, int height) {
            if (width <= 0 || height <= 0) {
                return {};
            }

            const float targetAspect =
                static_cast<float>(
                    PRESENTATION::kDefaultLogicalWidth) /
                static_cast<float>(
                    PRESENTATION::kDefaultLogicalHeight);
            const float outputAspect =
                static_cast<float>(width) / static_cast<float>(height);
            int viewportWidth = width;
            int viewportHeight = height;
            int viewportX = 0;
            int viewportY = 0;
            if (outputAspect > targetAspect) {
                viewportWidth = static_cast<int>(height * targetAspect + 0.5f);
                viewportX = (width - viewportWidth) / 2;
            } else {
                viewportHeight = static_cast<int>(width / targetAspect + 0.5f);
                viewportY = (height - viewportHeight) / 2;
            }
            return {
                static_cast<float>(viewportX),
                static_cast<float>(viewportY),
                static_cast<float>(viewportWidth),
                static_cast<float>(viewportHeight)
            };
        }
    }

    void PostPresentationStage::UpdateContext(const GFX::Context& context) {
        context_ = context;
        editorOutput_.UpdateContext(context);
    }

    void PostPresentationStage::Shutdown() {
        editorOutput_.Finalize();
        editorOutputSrvCpu_ = {};
        editorOutputSrvGpu_ = {};
        editorOutputReady_ = false;
    }

    void PostPresentationStage::InvalidateEditorOutput() {
        editorOutputReady_ = false;
    }

    bool PostPresentationStage::EnsureEditorOutput(int width, int height) {
        if (width <= 0 || height <= 0) {
            return false;
        }
        editorOutput_.UpdateContext(context_);
        const bool invalid =
            editorOutput_.GetResource() == nullptr ||
            editorOutput_.GetWidth() != width ||
            editorOutput_.GetHeight() != height ||
            editorOutput_.GetFormat() != DXGI_FORMAT_R8G8B8A8_UNORM ||
            editorOutput_.HasDepth();
        if (!invalid) {
            return true;
        }

        editorOutput_.Finalize();
        editorOutput_.SetDebugName("Presentation.EditorGameView.LDR");
        if (!editorOutput_.Init(
                width,
                height,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                false,
                { 0.0f, 0.0f, 0.0f, 1.0f })) {
            editorOutputReady_ = false;
            DEBUGLOG::PushRenderError(
                "[PresentationStage][ERROR] Editor output creation failed.");
            GFX::DumpD3D12InfoQueue(
                context_.device,
                "Presentation editor output creation failed");
            return false;
        }
        RefreshEditorOutputSrv();
        return true;
    }

    void PostPresentationStage::RefreshEditorOutputSrv() {
        if (context_.device == nullptr ||
            context_.srvHeap == nullptr ||
            editorOutput_.GetResource() == nullptr) {
            editorOutputSrvCpu_ = {};
            editorOutputSrvGpu_ = {};
            editorOutputReady_ = false;
            return;
        }

        const UINT descriptorSize = context_.device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT descriptorIndex = GFX::DESCRIPTOR::ToIndex(
            GFX::DESCRIPTOR::SystemSrv::EditorViewport);
        editorOutputSrvCpu_ = GFX::DESCRIPTOR::CpuAt(
            context_.srvHeap,
            descriptorSize,
            descriptorIndex);
        editorOutputSrvGpu_ = GFX::DESCRIPTOR::GpuAt(
            context_.srvHeap,
            descriptorSize,
            descriptorIndex);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        context_.device->CreateShaderResourceView(
            editorOutput_.GetResource(),
            &srv,
            editorOutputSrvCpu_);
        editorOutputReady_ = true;
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

        if (!EnsureEditorOutput(source.GetWidth(), source.GetHeight())) {
            BindBackBufferFullViewport();
            return false;
        }

        editorOutput_.BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
        const bool drew = DrawSource(source, quad);
        editorOutput_.EndCapture();
        RefreshEditorOutputSrv();
        BindBackBufferFullViewport();
        editorOutputReady_ =
            drew && IsEditorOutputReady(expectedWidth, expectedHeight);
        return editorOutputReady_;
    }

    bool PostPresentationStage::PresentToBackBuffer(
        RenderTarget2D& source,
        QuadDrawer& quad) {

        if (context_.cmdList == nullptr) {
            return false;
        }
        context_.cmdList->OMSetRenderTargets(1, &context_.rtv, FALSE, nullptr);
        const LetterboxRect rect = ComputeLetterboxRect(
            context_.backBufferWidth,
            context_.backBufferHeight);
        const D3D12_VIEWPORT viewport{
            rect.x, rect.y, rect.width, rect.height, 0.0f, 1.0f
        };
        const D3D12_RECT scissor{
            static_cast<LONG>(rect.x),
            static_cast<LONG>(rect.y),
            static_cast<LONG>(rect.x + rect.width),
            static_cast<LONG>(rect.y + rect.height)
        };
        context_.cmdList->RSSetViewports(1, &viewport);
        context_.cmdList->RSSetScissorRects(1, &scissor);
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

        const bool sizeCurrent =
            expectedWidth <= 0 ||
            expectedHeight <= 0 ||
            (editorOutput_.GetWidth() == expectedWidth &&
                editorOutput_.GetHeight() == expectedHeight);
        return editorOutputReady_ &&
            editorOutputSrvGpu_.ptr != 0 &&
            editorOutput_.GetResource() != nullptr &&
            sizeCurrent;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE
    PostPresentationStage::GetEditorOutputSrv() const {
        return editorOutputSrvGpu_;
    }

    int PostPresentationStage::GetEditorOutputWidth() const {
        return editorOutput_.GetWidth();
    }

    int PostPresentationStage::GetEditorOutputHeight() const {
        return editorOutput_.GetHeight();
    }

} // namespace HIKARI::POST
