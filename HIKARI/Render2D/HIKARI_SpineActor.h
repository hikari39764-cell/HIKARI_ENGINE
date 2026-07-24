#pragma once
#include <string>
#include "Render2D/HIKARI_Transform2D.h"
#include "Render2D/HIKARI_Renderer.h"

#include <spine/Atlas.h>
#include <spine/Skeleton.h>
#include <spine/SkeletonJson.h>
#include <spine/AnimationState.h>
#include <spine/AnimationStateData.h>
#include <spine/Attachment.h>
#include <spine/RegionAttachment.h>
#include <spine/Physics.h>
#include <spine/SkeletonRenderer.h>
#include <spine/EventData.h> 
#include <spine/Event.h> 
#include <functional>
namespace HIKARI {

    class SpineActor {
    public:
        Transform2D transform;
        bool enableCamera = true;
        SpineActor() = default;
        ~SpineActor();

        bool Load(const std::string& atlasPath, const std::string& jsonPath);
        void SetAnimation(const char* name, bool loop, int trackIndex = 0, float mixDuration = -1.0f, bool forceSet = false);
        void SetSkin(const char* skinName);

        void Update(float dt, bool usePhysics = false);
        void Draw();

        // --- Queue / Mix / Clear / Events---
        void AddAnimation(const char* name, bool loop, float delaySec, bool forceAdd = false);
        void SetMix(const char* from, const char* to, float mixSec);
        void SetEmptyAnimation(int track, float mixSec, bool forceSet = false);
        void ClearTrack(int track);
        void ClearTracks();
        using StateListener = std::function<void(spine::EventType, spine::TrackEntry*, spine::Event*)>;
        void SetStateListener(StateListener cb);
        bool IsTrackFinished(int trackIndex = 0);
        bool IsAnimationFinished(const char* name, int trackIndex = 0);
    private:
        class ListenerProxy : public spine::AnimationStateListenerObject {
        public:
            SpineActor::StateListener cb;
            explicit ListenerProxy(SpineActor::StateListener f) : cb(std::move(f)) {}
            void callback(spine::AnimationState* /*state*/,
                spine::EventType type,
                spine::TrackEntry* entry,
                spine::Event* event) override {
                if (cb) cb(type, entry, event);
            }
        };
        ListenerProxy* listener_ = nullptr;

        spine::SkeletonRenderer renderer_;
        spine::Atlas* atlas_{ nullptr };
        spine::SkeletonData* skelData_{ nullptr };
        spine::Skeleton* skeleton_{ nullptr };
        spine::AnimationStateData* stateData_{ nullptr };
        spine::AnimationState* state_{ nullptr };
    };

} // namespace HIKARI
