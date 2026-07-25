#include "Editor/Host/HIKARI_EditorUiServices.h"

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Editor/HIKARI_EditorStyle.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Style/HIKARI_EditorFontManager.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_Dx12Core.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Platform/HIKARI_Win32Window.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Runtime/Host/HIKARI_IEditorUiServices.h"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam);

namespace HIKARI::EDITOR {

    namespace {

        class EditorUiServices final
            : public RUNTIME::IEditorUiServices {
        public:
            bool Initialize(
                PLATFORM::Win32Window& window,
                const GFX::Context& graphics) override;
            void BeginFrame(
                PLATFORM::Win32Window& window,
                const GFX::Context& graphics,
                float unscaledDeltaTime,
                bool produceEditorUiFrame) override;
            void RenderFrame(
                const GFX::Context& graphics,
                bool produceEditorUiFrame) override;
            void Shutdown(
                PLATFORM::Win32Window& window) noexcept override;

        private:
            void ConfigureContext();
            bool InitializeBackend(
                PLATFORM::Win32Window& window,
                const GFX::Context& graphics);

            bool contextInitialized_ = false;
            bool backendInitialized_ = false;
            bool frameBegun_ = false;
            D3D12_CPU_DESCRIPTOR_HANDLE fontSrvCpu_{};
            D3D12_GPU_DESCRIPTOR_HANDLE fontSrvGpu_{};
        };

        EditorUiServices gEditorUiServices{};

        void EditorUiServices::ConfigureContext() {
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigDockingWithShift = false;
            io.ConfigDockingTransparentPayload = true;
            io.ConfigViewportsNoAutoMerge = false;
            io.ConfigViewportsNoTaskBarIcon = false;
            (void)EnsureEditorFonts();
            ApplyEditorStyle();
        }

        bool EditorUiServices::InitializeBackend(
            PLATFORM::Win32Window& window,
            const GFX::Context& graphics) {

            if (backendInitialized_) {
                return true;
            }
            if (graphics.device == nullptr ||
                graphics.srvHeap == nullptr ||
                window.GetHWND() == nullptr) {
                return false;
            }

            const UINT descriptorSize =
                graphics.device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const UINT fontSrvIndex =
                GFX::DESCRIPTOR::ToIndex(
                    GFX::DESCRIPTOR::SystemSrv::ImGuiFont);
            fontSrvCpu_ = GFX::DESCRIPTOR::CpuAt(
                graphics.srvHeap,
                descriptorSize,
                fontSrvIndex);
            fontSrvGpu_ = GFX::DESCRIPTOR::GpuAt(
                graphics.srvHeap,
                descriptorSize,
                fontSrvIndex);

            if (!ImGui_ImplWin32_Init(window.GetHWND())) {
                HIKARI_LOG_ERROR("ImGui_ImplWin32_Init failed.");
                return false;
            }
            if (!ImGui_ImplDX12_Init(
                    graphics.device,
                    static_cast<int>(GFX::kFrameResourceCount),
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    graphics.srvHeap,
                    fontSrvCpu_,
                    fontSrvGpu_)) {
                HIKARI_LOG_ERROR("ImGui_ImplDX12_Init failed.");
                ImGui_ImplWin32_Shutdown();
                return false;
            }

            backendInitialized_ = true;
            HIKARI_LOG_INFO("ImGui backend initialized.");
            return true;
        }

        bool EditorUiServices::Initialize(
            PLATFORM::Win32Window& window,
            const GFX::Context& graphics) {

            if (!contextInitialized_) {
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                contextInitialized_ = true;
                ConfigureContext();
                HIKARI_LOG_INFO("ImGui context initialized.");
            }

            window.SetMessageHandler(
                [](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
                    return ImGui_ImplWin32_WndProcHandler(
                        hwnd,
                        message,
                        wparam,
                        lparam) != 0;
                });

            (void)InitializeBackend(window, graphics);
            return true;
        }

        void EditorUiServices::BeginFrame(
            PLATFORM::Win32Window& window,
            const GFX::Context& graphics,
            float unscaledDeltaTime,
            bool produceEditorUiFrame) {

            if (!produceEditorUiFrame) {
                ClearGameViewportInputRect();
                return;
            }
            if (!contextInitialized_ ||
                ImGui::GetCurrentContext() == nullptr) {
                return;
            }

            ConfigureContext();
            if (!backendInitialized_) {
                (void)InitializeBackend(window, graphics);
            }
            if (!backendInitialized_) {
                return;
            }

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();

            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(
                static_cast<float>(window.Width()),
                static_cast<float>(window.Height()));
            io.DeltaTime = unscaledDeltaTime > 0.0f
                ? unscaledDeltaTime
                : (1.0f / 60.0f);
            if (io.Fonts != nullptr && io.Fonts->Fonts.empty()) {
                (void)EnsureEditorFonts();
            }

            ImGui::NewFrame();
            frameBegun_ = true;
        }

        void EditorUiServices::RenderFrame(
            const GFX::Context& graphics,
            bool produceEditorUiFrame) {

            if (!contextInitialized_ || !frameBegun_) {
                return;
            }

            ImGui::Render();
            frameBegun_ = false;
            if (!produceEditorUiFrame ||
                !backendInitialized_ ||
                graphics.cmdList == nullptr ||
                graphics.srvHeap == nullptr) {
                return;
            }

            CPU_PROFILE::ScopedCpuTimer cpuImGui(
                CPU_PROFILE::Pass::ImGui);
            GFX::PIX::ScopedGpuEvent pixImGui(
                graphics.cmdList,
                GFX::PIX::kColorEditor,
                "ImGui");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuImGui(
                graphics.cmdList,
                GFX::GPU_PROFILE::Pass::ImGui);

            ID3D12DescriptorHeap* heaps[] = { graphics.srvHeap };
            graphics.cmdList->SetDescriptorHeaps(1, heaps);
            ImGui_ImplDX12_RenderDrawData(
                ImGui::GetDrawData(),
                graphics.cmdList);

            ImGuiIO& io = ImGui::GetIO();
            if ((io.ConfigFlags &
                 ImGuiConfigFlags_ViewportsEnable) != 0) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }

        void EditorUiServices::Shutdown(
            PLATFORM::Win32Window& window) noexcept {

            window.SetMessageHandler({});
            frameBegun_ = false;

            if (backendInitialized_) {
                ImGui_ImplDX12_Shutdown();
                ImGui_ImplWin32_Shutdown();
                backendInitialized_ = false;
                HIKARI_LOG_INFO("ImGui backend shutdown.");
            }
            if (contextInitialized_) {
                ResetEditorFonts();
                ImGui::DestroyContext();
                contextInitialized_ = false;
                HIKARI_LOG_INFO("ImGui shutdown.");
            }

            RENDER3D::EDITORVIEW::Shutdown();
            HIKARI_LOG_INFO(
                "Editor interactive view renderer shutdown.");
        }

    } // namespace

    bool InstallEditorUiServices() noexcept {
        return RUNTIME::RegisterEditorUiServices(gEditorUiServices);
    }

    void UninstallEditorUiServices() noexcept {
        RUNTIME::UnregisterEditorUiServices(gEditorUiServices);
    }

} // namespace HIKARI::EDITOR
