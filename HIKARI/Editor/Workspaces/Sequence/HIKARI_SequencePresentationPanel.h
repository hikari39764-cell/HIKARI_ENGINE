#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Animation/Runtime/HIKARI_AnimationPose.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI {

    class AssetDatabase;
    class DocumentSceneBase;
    class World;

    namespace EDITOR {

        enum class SequencePresentationItemKind : uint8_t {
            None,
            ActorAnimation,
            Vfx,
            TimeDilation,
            Signal,
        };

        struct SequencePresentationPanelResult {
            bool sequenceChanged = false;
            bool selectionChanged = false;
            std::optional<float> requestedPlayheadSeconds{};
            uint64_t editMergeId = 0;
        };

        class SequencePresentationPanel {
        public:
            SequencePresentationPanelResult DrawTimeline(
                DocumentSceneBase& scene,
                CinematicSequence& sequence,
                const AssetGuid& sequenceAssetGuid,
                SEQUENCER::SequenceBindingContext& previewBindings,
                SceneObjectId selectedObjectId,
                float playheadSeconds,
                bool playing,
                bool editingAllowed,
                bool portableAsset);
            SequencePresentationPanelResult DrawInspector(
                AssetDatabase& assetDatabase,
                CinematicSequence& sequence,
                bool editingAllowed);

            void ApplyActorPreview(
                World& world,
                const CinematicSequence& sequence,
                const SEQUENCER::SequenceBindingContext& bindings,
                float timeSeconds);
            void RestoreActorPreview(World& world) noexcept;
            void Reset(World* world = nullptr) noexcept;

            SequencePresentationItemKind GetSelectedKind() const noexcept;
            uint64_t GetSelectedItemId() const noexcept;

        private:
            enum class DragMode : uint8_t {
                None,
                Move,
                ResizeStart,
                ResizeEnd,
            };

            struct PreviewAnimatorSnapshot {
                SceneObjectId objectId{};
                ANIMATION::AnimationMotionSample motion{};
                float timeSeconds = 0.0f;
                float speed = 1.0f;
                bool loop = true;
                bool autoPlay = true;
                bool rootMotionEnabled = false;
                bool playing = false;
                uint64_t activeClipId = 0;
            };

            void Select(
                SequencePresentationItemKind kind,
                uint64_t itemId) noexcept;
            void NormalizeSelection(const CinematicSequence& sequence) noexcept;
            bool DeleteSelection(CinematicSequence& sequence);
            float SnapTime(float value) const noexcept;
            SEQUENCER::SequenceBindingId ResolveActorBinding(
                DocumentSceneBase& scene,
                CinematicSequence& sequence,
                SceneObjectId selectedObjectId,
                bool portableAsset,
                SEQUENCER::SequenceBindingContext& previewBindings);
            bool HasAnimator(
                const DocumentSceneBase& scene,
                SceneObjectId objectId) const;
            PreviewAnimatorSnapshot& CaptureAnimator(
                World& world,
                SceneObjectId objectId);
            void RestoreInactivePreviewActors(
                World& world,
                const std::vector<SceneObjectId>& activeObjects) noexcept;

            SequencePresentationItemKind selectedKind_ =
                SequencePresentationItemKind::None;
            uint64_t selectedItemId_ = 0;
            SEQUENCER::SequenceBindingId selectedBindingId_{};
            std::vector<PreviewAnimatorSnapshot> previewAnimators_{};
            float pixelsPerSecond_ = 88.0f;
            float scrollTimeSeconds_ = 0.0f;
            float panStartMouseX_ = 0.0f;
            float panStartScrollTime_ = 0.0f;
            float dragStartMouseX_ = 0.0f;
            float dragStartTime_ = 0.0f;
            float dragStartDuration_ = 0.0f;
            int snapFramesPerSecond_ = 30;
            DragMode dragMode_ = DragMode::None;
            bool snapEnabled_ = true;
            bool panning_ = false;
            bool scrubbing_ = false;
        };

    } // namespace EDITOR
} // namespace HIKARI
