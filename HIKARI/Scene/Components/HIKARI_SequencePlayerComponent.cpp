#include "Scene/Components/HIKARI_SequencePlayerComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>

#if defined(HIKARI_WITH_EDITOR)
#include <memory>

#include "Assets/HIKARI_DocumentAssetStores.h"
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {

namespace {
constexpr size_t kMaximumBindings = 32;
constexpr float kMinimumPlaybackRate = 0.01f;

// 範囲へ丸める。逆再生（負の速さ）は残し、止まってしまう 0 付近は最小の速さにする。
SequencePlayerSettings Normalize(SequencePlayerSettings settings) {
    settings = JSONFIELDS::Sanitize(std::move(settings), kSequencePlayerFields);
    const float magnitude = (std::max)(std::abs(settings.playbackRate), kMinimumPlaybackRate);
    settings.playbackRate = settings.playbackRate < 0.0f ? -magnitude : magnitude;
    if (settings.bindings.size() > kMaximumBindings) {
        settings.bindings.resize(kMaximumBindings);
    }
    return settings;
}
} // namespace

void SequencePlayerComponent::OnAttach() { ResetRuntimeState(); }

void SequencePlayerComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kSequencePlayerFields);
}

void SequencePlayerComponent::Deserialize(const nlohmann::json& in) {
    SequencePlayerSettings decoded{};
    JSONFIELDS::Read(in, decoded, kSequencePlayerFields);
    settings_ = Normalize(std::move(decoded));
    ResetRuntimeState();
}

#if defined(HIKARI_WITH_EDITOR)
void SequencePlayerComponent::BuildInspector(IInspectorBuilder& builder) {
    SequencePlayerSettings next = settings_;
    const bool edited = InspectFields(builder, next, kSequencePlayerFields);
    const bool assetChanged = next.sequenceAssetGuid != settings_.sequenceAssetGuid;
    if (edited) {
        settings_ = Normalize(std::move(next));
    }

    // 束縛スロットは、資産を替えた時とボタンを押した時に、資産が宣言するスロットへ合わせる（当てた物体は残す）。
    const bool syncBindingsRequested = builder.Button("Sync Binding Slots");
    if (assetChanged || syncBindingsRequested) {
        const InspectorContext& context = builder.GetContext();
        if (context.assetDatabase == nullptr || !settings_.sequenceAssetGuid.IsValid()) {
            lastDiagnostic_ = "Assign a valid Sequence Asset before synchronizing slots.";
        } else {
            SequenceAssetStore store{};
            store.SetAssetDatabase(context.assetDatabase);
            std::string error{};
            const std::shared_ptr<const SequenceAsset> asset = store.Load(settings_.sequenceAssetGuid, &error);
            if (!asset) {
                lastDiagnostic_ = error.empty() ? "Sequence Asset could not be loaded." : error;
            } else {
                std::vector<SequencePlayerBinding> synchronized{};
                for (const SEQUENCER::SequenceBinding& assetBinding : asset->sequence.bindings) {
                    if (assetBinding.targetKind != SEQUENCER::SequenceBindingTargetKind::Slot ||
                        assetBinding.slotName.empty() || assetBinding.slotName == settings_.ownerSlotName) {
                        continue;
                    }
                    const auto existing = std::find_if(settings_.bindings.begin(), settings_.bindings.end(),
                                                       [&](const SequencePlayerBinding& binding) {
                                                           return binding.slotName == assetBinding.slotName;
                                                       });
                    synchronized.push_back({assetBinding.slotName, existing != settings_.bindings.end()
                                                                       ? existing->sceneObjectId
                                                                       : SceneObjectId{}});
                }
                settings_.bindings = std::move(synchronized);
                settings_ = Normalize(std::move(settings_));
                lastDiagnostic_ = "Binding slots synchronized from asset.";
            }
        }
    }
    if (!lastDiagnostic_.empty()) {
        builder.Text(lastDiagnostic_);
    }
}
#endif

void SequencePlayerComponent::Play() { pendingAction_ = PendingAction::Play; }

void SequencePlayerComponent::Stop() { pendingAction_ = PendingAction::Stop; }

void SequencePlayerComponent::Pause() { pendingAction_ = PendingAction::Pause; }

void SequencePlayerComponent::Resume() { pendingAction_ = PendingAction::Resume; }

void SequencePlayerComponent::Seek(float timeSeconds) {
    pendingSeekTimeSeconds_ = std::isfinite(timeSeconds) ? (std::max)(timeSeconds, 0.0f) : 0.0f;
    pendingAction_ = PendingAction::Seek;
}

bool SequencePlayerComponent::IsEnabled() const noexcept { return settings_.enabled; }

bool SequencePlayerComponent::IsPlaying() const noexcept { return playbackHandle_.IsValid() && !runtimePaused_; }

bool SequencePlayerComponent::IsPaused() const noexcept { return playbackHandle_.IsValid() && runtimePaused_; }

void SequencePlayerComponent::ResetRuntimeState() {
    playbackHandle_ = {};
    pendingAction_ = PendingAction::None;
    pendingSeekTimeSeconds_ = 0.0f;
    autoPlayPending_ = settings_.playOnStart;
    runtimePaused_ = false;
    lastDiagnostic_.clear();
}

} // namespace HIKARI
