#include "Editor/Views/HIKARI_GameUiPreviewTarget.h"
#include "Render3D/Ui/HIKARI_GameUiEffectRenderer.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::EDITOR {
D3D12_GPU_DESCRIPTOR_HANDLE
GameUiPreviewTarget::Render(const GAME_UI::GameUiFrameSource& source, uint32_t width, uint32_t height,
                            GameProjectUiColor clear) {
    if (width < 64 || height < 64 || width > 2048 || height > 2048)
        return {};
    const auto& graphics = GFX::GetCurrentContext();
    if (!graphics.device || !graphics.cmdList)
        return {};
    if (target_.deviceGeneration != graphics.deviceGeneration)
        Reset();
    if (fence_ == graphics.currentFrameRetireFenceValue)
        return published_;
    // ライブなプレビューごとに所有し、ゲーム用 Canvas スロットとは共有しない。
    const auto owner = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
    target_.memoryOwner = GFX::VIDEO_MEMORY::Owner::Editor;
    if (!target_.Ensure(graphics, owner, 0, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, false,
                        {clear.r, clear.g, clear.b, clear.a}))
        return {};
    auto& target = *target_.targets[target_.writeIndex];
    target.BeginCapture(clear.r, clear.g, clear.b, clear.a);
    const bool ok = GAME_UI::RenderGameUiWorkspacePreviewFrame(source, width, height, &failedTextRequests_);
    target.EndCapture();
    (void)POST::PostSystem::RebindPresentationBackBuffer();
    published_ = ok ? RENDER3D::GetTextureResourceSrvGpuHandle(target_.textures[target_.writeIndex])
                    : D3D12_GPU_DESCRIPTOR_HANDLE{};
    target_.writeIndex = (target_.writeIndex + 1) % GFX::kFrameResourceCount;
    fence_ = graphics.currentFrameRetireFenceValue;
    return published_;
}
} // namespace HIKARI::EDITOR
