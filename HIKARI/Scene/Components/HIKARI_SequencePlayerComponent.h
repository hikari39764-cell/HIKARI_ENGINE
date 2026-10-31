#pragma once

#include <string>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetTypeEnum.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackTypes.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

struct SequencePlayerBinding {
    std::string slotName{};
    SceneObjectId sceneObjectId{};
};

inline constexpr TEXT::EnumName<SEQUENCER::SequenceChannelPolicy> kSequenceChannelPolicyNames[] = {
    {SEQUENCER::SequenceChannelPolicy::Parallel, "Parallel"},
    {SEQUENCER::SequenceChannelPolicy::Replace, "Replace"},
    {SEQUENCER::SequenceChannelPolicy::ReplaceIfHigherOrEqual, "ReplaceIfHigherOrEqual"},
    {SEQUENCER::SequenceChannelPolicy::RejectIfOccupied, "RejectIfOccupied"},
};

// 作者値（シーン文書の properties と同じ形）。項目表（kSequencePlayerFields）が保存・読み取り・検査器・説明を決める。
struct SequencePlayerSettings {
    bool enabled = true;
    AssetGuid sequenceAssetGuid{};
    bool playOnStart = false;
    bool loop = false;
    bool holdAtEnd = false;
    float playbackRate = 1.0f;
    float startTimeSeconds = 0.0f;
    std::string channel = "Cinematics";
    int priority = 0;
    SEQUENCER::SequenceChannelPolicy channelPolicy = SEQUENCER::SequenceChannelPolicy::ReplaceIfHigherOrEqual;
    bool stopOnDisable = true;
    bool restartIfPlaying = true;
    std::string ownerSlotName = "Owner";
    std::vector<SequencePlayerBinding> bindings{};
};

inline constexpr auto kSequencePlayerBindingFields = [](auto&& field) {
    using S = SequencePlayerBinding;
    using namespace JSONFIELDS;
    field("slotName", &S::slotName, Label{"Slot"}, Help{"Sequence binding slot name."});
    field("sceneObjectId", &S::sceneObjectId, Label{"Object"}, Help{"Scene object assigned to this slot."},
          ObjectRef{});
};

inline constexpr auto kSequencePlayerFields = [](auto&& field) {
    using S = SequencePlayerSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("sequenceAssetGuid", &S::sequenceAssetGuid, Label{"Sequence Asset"},
          Help{"Sequence asset played by this component; assigning synchronizes binding slots."},
          AssetRef{AssetType::Sequence}, Common{});
    field("playOnStart", &S::playOnStart, Label{"Play On Start"},
          Help{"Automatically play the assigned sequence when the scene starts."}, Common{});
    field("loop", &S::loop, Label{"Loop"}, Common{});
    field("holdAtEnd", &S::holdAtEnd, Label{"Hold At End"});
    field("playbackRate", &S::playbackRate, Range{-8.0, 8.0}, Label{"Playback Rate"});
    field("startTimeSeconds", &S::startTimeSeconds, Range{0.0, 3600.0}, Label{"Start Time"},
          Help{"Playback offset in seconds."});
    field("channel", &S::channel, Label{"Channel"});
    field("priority", &S::priority, Label{"Priority"});
    field("channelPolicy", &S::channelPolicy, kSequenceChannelPolicyNames, Label{"Channel Policy"},
          Help{"How playback shares the selected channel."});
    field("stopOnDisable", &S::stopOnDisable, Label{"Stop On Disable"});
    field("restartIfPlaying", &S::restartIfPlaying, Label{"Restart If Playing"});
    field("ownerSlotName", &S::ownerSlotName, Label{"Owner Slot"});
    field("bindings", &S::bindings, Label{"Binding"}, Elements{kSequencePlayerBindingFields});
};

class SequencePlayerSystem;

class SequencePlayerComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "SequencePlayerComponent"; }

    void OnAttach() override;
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    void Play();
    void Stop();
    void Pause();
    void Resume();
    void Seek(float timeSeconds);

    bool IsEnabled() const noexcept;
    bool IsPlaying() const noexcept;
    bool IsPaused() const noexcept;

private:
    friend class SequencePlayerSystem;

    enum class PendingAction : uint8_t {
        None,
        Play,
        Stop,
        Pause,
        Resume,
        Seek,
    };

    void ResetRuntimeState();

    SequencePlayerSettings settings_{};

    SEQUENCER::SequencePlaybackHandle playbackHandle_{};
    PendingAction pendingAction_ = PendingAction::None;
    float pendingSeekTimeSeconds_ = 0.0f;
    bool autoPlayPending_ = false;
    bool runtimePaused_ = false;
    std::string lastDiagnostic_{};
};

} // namespace HIKARI
