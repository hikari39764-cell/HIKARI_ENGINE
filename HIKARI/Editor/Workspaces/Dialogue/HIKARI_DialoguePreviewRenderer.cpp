#include "Editor/Workspaces/Dialogue/HIKARI_DialoguePreviewRenderer.h"

#include <algorithm>
#include <array>
#include <utility>

#include "Gfx/HIKARI_GfxContext.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Gameplay/Dialogue/HIKARI_DialoguePresentation.h"
#include "Render2D/HIKARI_GameUiEffectRenderer.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::EDITOR {
namespace {
constexpr DXGI_FORMAT kPreviewFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr uint32_t kMinimumPreviewWidth = 320u;
constexpr uint32_t kMinimumPreviewHeight = 180u;
constexpr uint32_t kMaximumPreviewWidth = 1600u;
constexpr uint32_t kMaximumPreviewHeight = 900u;
} // namespace

struct DialoguePreviewRenderer::Impl {
    std::unique_ptr<RenderTarget2D> target{};
    std::array<RENDER3D::RenderResourceView, GFX::kFrameResourceCount> outputSrvs{};
    std::array<ID3D12Resource*, GFX::kFrameResourceCount> outputResources{};
    std::filesystem::path projectRoot{};
    GAME_UI::GameUiPresentationService presentation{};
    D3D12_GPU_DESCRIPTOR_HANDLE output{};
    uint64_t previewFrame = 0u;
    std::string status{"Preview waiting for a valid dialogue"};

    bool EnsureTarget(uint32_t requestedWidth, uint32_t requestedHeight) {
        const GFX::Context& graphics = GFX::GetCurrentContext();
        if (graphics.device == nullptr || graphics.cmdList == nullptr) {
            status = "Preview waiting for a graphics frame";
            return false;
        }
        const uint32_t width = std::clamp(requestedWidth, kMinimumPreviewWidth, kMaximumPreviewWidth);
        const uint32_t height = std::clamp(requestedHeight, kMinimumPreviewHeight, kMaximumPreviewHeight);
        if (target != nullptr && target->IsInitialized() && target->GetWidth() == static_cast<int>(width) &&
            target->GetHeight() == static_cast<int>(height)) {
            target->UpdateContext(graphics);
            return true;
        }

        auto replacement = std::make_unique<RenderTarget2D>();
        replacement->UpdateContext(graphics);
        if (!replacement->Init(static_cast<int>(width), static_cast<int>(height), kPreviewFormat, false,
                               {0.12f, 0.14f, 0.18f, 1.0f}, false, false)) {
            status = "Unable to allocate the dialogue preview target";
            return false;
        }
        replacement->SetDebugName("DialogueWorkspace.UiPreview");
        target = std::move(replacement);
        outputResources.fill(nullptr);
        return true;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE PublishOutput() {
        D3D12_GPU_DESCRIPTOR_HANDLE result{};
        const GFX::Context& graphics = GFX::GetCurrentContext();
        if (target == nullptr || target->GetResource() == nullptr || graphics.device == nullptr) {
            return result;
        }
        RENDER3D::UpdateRenderResourceDescriptorPoolContext(graphics);
        const uint32_t frameSlot = graphics.frameIndex % GFX::kFrameResourceCount;
        RENDER3D::RenderResourceView& view = outputSrvs[frameSlot];
        if (!view.IsValid()) {
            view = RENDER3D::AllocateTexture2DSrvDescriptor(target->GetResource(), target->GetFormat());
            if (!view.IsValid())
                return result;
            outputResources[frameSlot] = target->GetResource();
        } else if (outputResources[frameSlot] != target->GetResource()) {
            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.Format = target->GetFormat();
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MostDetailedMip = 0u;
            desc.Texture2D.MipLevels = 1u;
            graphics.device->CreateShaderResourceView(target->GetResource(), &desc, view.cpu);
            outputResources[frameSlot] = target->GetResource();
        }
        return view.gpu;
    }
};

DialoguePreviewRenderer::DialoguePreviewRenderer() : impl_(std::make_unique<Impl>()) {}

DialoguePreviewRenderer::~DialoguePreviewRenderer() { Shutdown(); }

bool DialoguePreviewRenderer::Render(const AssetDatabase& database, const DIALOGUE::DialogueDefinition& definition,
                                     const DIALOGUE::DialogueProjectStyle& style, const DIALOGUE::DialogueNode& node,
                                     std::string_view locale, size_t selectedChoice, float sessionElapsed,
                                     float revealAmount, uint32_t width, uint32_t height) noexcept {
    bool previewCaptureStarted = false;
    try {
        if (impl_ == nullptr || !impl_->EnsureTarget(width, height)) {
            return false;
        }
        const auto& projectRoot = database.GetProjectRoot();
        if (impl_->projectRoot != projectRoot) {
            impl_->projectRoot = projectRoot;
            impl_->presentation.SetProjectRoot(projectRoot);
        }
        impl_->presentation.SetAssetDatabase(&database);

        DIALOGUE::DialoguePresentationState state{};
        state.definition = &definition;
        state.style = &style;
        state.node = &node;
        state.locale = locale;
        state.sourceIdBase = 0xD1A1060Eull;
        state.selectedChoice = selectedChoice;
        state.sessionElapsed = sessionElapsed;
        state.revealAmount = revealAmount;
        const DIALOGUE::DialogueSpeakerProfile* speaking = DIALOGUE::FindDialogueSpeaker(style, node.speakerId);
        const DIALOGUE::DialogueSpeakerProfile* other = nullptr;
        for (const auto& candidate : style.speakers) {
            if (!candidate.resolvedPortraitAssetPath.empty() && (&candidate != speaking)) {
                other = &candidate;
                break;
            }
        }
        if (speaking == nullptr) {
            for (const auto& candidate : style.speakers) {
                if (!candidate.resolvedPortraitAssetPath.empty()) {
                    speaking = &candidate;
                    break;
                }
            }
        }
        const bool portraitExpected =
            speaking != nullptr && node.portraitAction != DIALOGUE::DialoguePortraitAction::Hide;
        const bool portraitResolved = portraitExpected && !speaking->resolvedPortraitAssetPath.empty();
        if (portraitExpected) {
            DIALOGUE::DialoguePortraitSlot slot = node.portraitSlot;
            if (slot == DIALOGUE::DialoguePortraitSlot::Auto) {
                slot = speaking->preferredSlot;
            }
            if (slot == DIALOGUE::DialoguePortraitSlot::Right) {
                state.rightPortrait = {speaking->id, 1.0f, 1.0f};
                if (other != nullptr) {
                    state.leftPortrait = {other->id, 1.0f, 0.0f};
                }
            } else {
                state.leftPortrait = {speaking->id, 1.0f, 1.0f};
                if (other != nullptr) {
                    state.rightPortrait = {other->id, 1.0f, 0.0f};
                }
            }
        }
        if (!DIALOGUE::SubmitDialoguePresentation(impl_->presentation, state)) {
            impl_->status = "Dialogue preview submission failed";
            return false;
        }
        ++impl_->previewFrame;
        if (impl_->previewFrame == 0u)
            ++impl_->previewFrame;
        impl_->presentation.Tick(impl_->previewFrame);
        const auto& frameSource = impl_->presentation.GetFrameSource();
        const bool portraitSubmitted = std::any_of(
            frameSource.sprites.begin(), frameSource.sprites.end(), [](const GAME_UI::GameUiSpriteDraw& draw) {
                return draw.request.effect == GameProjectUiEffect::DialoguePortraitFocus;
            });

        RenderTarget2D& target = *impl_->target;
        target.UpdateContext(GFX::GetCurrentContext());
        target.BeginCapture(0.12f, 0.14f, 0.18f, 1.0f);
        previewCaptureStarted = true;
        const bool rendered = GAME_UI::RenderGameUiPreviewFrame(
            impl_->presentation.GetFrameSource(), impl_->previewFrame, target.GetWidth(), target.GetHeight());
        target.EndCapture();
        previewCaptureStarted = false;
        if (!POST::PostSystem::RebindPresentationBackBuffer()) {
            impl_->output = {};
            impl_->status = "Dialogue preview could not restore the Editor output";
            return false;
        }
        impl_->output = impl_->PublishOutput();
        if (!rendered || impl_->output.ptr == 0u) {
            impl_->status = rendered ? "Dialogue preview output is unavailable" : "Dialogue preview renderer failed";
            return false;
        }
        if (node.portraitAction == DIALOGUE::DialoguePortraitAction::Hide) {
            impl_->status = "Live preview: this story step hides portraits";
        } else if (!portraitExpected) {
            impl_->status = "Live preview: select a registered character to show a portrait";
        } else if (!portraitResolved) {
            impl_->status = "Live preview: the selected character has no available portrait texture";
        } else if (!portraitSubmitted) {
            impl_->status = "Live preview: portrait submission was rejected";
        } else {
            impl_->status = "Live portrait preview";
        }
        return true;
    } catch (...) {
        if (impl_ != nullptr) {
            if (previewCaptureStarted && impl_->target != nullptr) {
                impl_->target->EndCapture();
            }
            impl_->status = "Dialogue preview raised an exception";
            impl_->output = {};
        }
        (void)POST::PostSystem::RebindPresentationBackBuffer();
        return false;
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE
DialoguePreviewRenderer::GetOutputSrv() const noexcept {
    return impl_ != nullptr ? impl_->output : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

const std::string& DialoguePreviewRenderer::GetStatus() const noexcept {
    static const std::string unavailable{"Preview unavailable"};
    return impl_ != nullptr ? impl_->status : unavailable;
}

void DialoguePreviewRenderer::Shutdown() noexcept {
    if (impl_ == nullptr)
        return;
    for (RENDER3D::RenderResourceView& view : impl_->outputSrvs) {
        if (view.IsValid()) {
            (void)RENDER3D::ReleaseRenderResourceDescriptor(view);
            view = {};
        }
    }
    impl_->outputResources.fill(nullptr);
    impl_->target.reset();
    impl_->presentation.Reset();
    impl_->output = {};
}

} // namespace HIKARI::EDITOR
