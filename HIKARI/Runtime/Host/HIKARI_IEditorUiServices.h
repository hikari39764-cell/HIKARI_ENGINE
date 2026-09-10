#pragma once

#include <cstdint>

namespace HIKARI {

    namespace INPUT {
        struct InputPointerState;
    }

    namespace GFX {
        struct Context;
    }

    namespace PLATFORM {
        class Win32Window;
    }

    namespace RUNTIME {

        class IEditorUiServices {
        public:
            virtual ~IEditorUiServices() = default;

            virtual bool Initialize(
                PLATFORM::Win32Window& window,
                const GFX::Context& graphics) = 0;
            virtual void BeginFrame(
                PLATFORM::Win32Window& window,
                const GFX::Context& graphics,
                float unscaledDeltaTime,
                bool produceEditorUiFrame) = 0;
            virtual void RenderFrame(
                const GFX::Context& graphics,
                bool produceEditorUiFrame) = 0;
            virtual void Shutdown(
                PLATFORM::Win32Window& window) noexcept = 0;
            virtual bool IsGameViewportPlayInputActive() const noexcept = 0;
            virtual bool TryGetGameViewportPointer(
                INPUT::InputPointerState& pointer,
                uint32_t& width, uint32_t& height) const noexcept = 0;
        };

        bool RegisterEditorUiServices(
            IEditorUiServices& services) noexcept;
        void UnregisterEditorUiServices(
            IEditorUiServices& services) noexcept;
        IEditorUiServices* GetEditorUiServices() noexcept;

    } // namespace RUNTIME
} // namespace HIKARI
