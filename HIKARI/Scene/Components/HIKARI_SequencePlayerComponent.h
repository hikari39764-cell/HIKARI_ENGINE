#pragma once

#include <string>
#include <vector>

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackTypes.h"

namespace HIKARI {

    struct SequencePlayerBinding {
        std::string slotName{};
        SceneObjectId sceneObjectId{};
    };

    class SequencePlayerSystem;

    class SequencePlayerComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "SequencePlayerComponent";
        }

        void OnAttach() override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;
        void RenderImGui() override;

        void Play();
        void Stop();
        void Pause();
        void Resume();
        void Seek(float timeSeconds);

        bool IsEnabled() const noexcept;
        bool IsPlaying() const noexcept;
        bool IsPaused() const noexcept;
        SEQUENCER::SequencePlaybackHandle GetPlaybackHandle() const noexcept;
        const std::string& GetLastDiagnostic() const noexcept;

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

        void NormalizeSettings();
        void ResetRuntimeState();

        bool enabled_ = true;
        std::string sequenceAssetGuid_{};
        bool playOnStart_ = false;
        bool loop_ = false;
        float playbackRate_ = 1.0f;
        float startTimeSeconds_ = 0.0f;
        std::string channel_ = "Cinematics";
        int priority_ = 0;
        SEQUENCER::SequenceChannelPolicy channelPolicy_ =
            SEQUENCER::SequenceChannelPolicy::ReplaceIfHigherOrEqual;
        bool stopOnDisable_ = true;
        bool restartIfPlaying_ = true;
        std::string ownerSlotName_ = "Owner";
        std::vector<SequencePlayerBinding> bindings_{};

        SEQUENCER::SequencePlaybackHandle playbackHandle_{};
        PendingAction pendingAction_ = PendingAction::None;
        float pendingSeekTimeSeconds_ = 0.0f;
        bool autoPlayPending_ = false;
        bool runtimePaused_ = false;
        std::string lastDiagnostic_{};
    };

} // namespace HIKARI
