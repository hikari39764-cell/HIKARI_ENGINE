#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <d3d12.h>

namespace HIKARI::DIALOGUE {
    struct DialogueDefinition;
    struct DialogueNode;
    struct DialogueProjectStyle;
}

namespace HIKARI::EDITOR {

    // Editor-owned UI preview. It never reads a Scene, camera, runtime world,
    // or gameplay viewport; it renders the production dialogue presenter into
    // a dedicated 2D target.
    class DialoguePreviewRenderer final {
    public:
        DialoguePreviewRenderer();
        ~DialoguePreviewRenderer();

        DialoguePreviewRenderer(const DialoguePreviewRenderer&) = delete;
        DialoguePreviewRenderer& operator=(
            const DialoguePreviewRenderer&) = delete;

        bool Render(
            const std::filesystem::path& projectRoot,
            const DIALOGUE::DialogueDefinition& definition,
            const DIALOGUE::DialogueProjectStyle& style,
            const DIALOGUE::DialogueNode& node,
            std::string_view locale,
            size_t selectedChoice,
            float sessionElapsed,
            float revealAmount,
            uint32_t width,
            uint32_t height) noexcept;
        D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSrv() const noexcept;
        const std::string& GetStatus() const noexcept;
        void Shutdown() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_{};
    };

} // namespace HIKARI::EDITOR
