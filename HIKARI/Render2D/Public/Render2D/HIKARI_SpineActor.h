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
namespace HIKARI {

class SpineActor {
public:
    Transform2D transform;
    bool enableCamera = true;
    SpineActor() = default;
    ~SpineActor();

    bool Load(const std::string& atlasPath, const std::string& jsonPath);
    void SetAnimation(const char* name, bool loop, int trackIndex = 0, float mixDuration = -1.0f,
                      bool forceSet = false);

    void Update(float dt, bool usePhysics = false);
    void Draw();

    bool IsAnimationFinished(const char* name, int trackIndex = 0);

private:
    spine::SkeletonRenderer renderer_;
    spine::Atlas* atlas_{nullptr};
    spine::SkeletonData* skelData_{nullptr};
    spine::Skeleton* skeleton_{nullptr};
    spine::AnimationStateData* stateData_{nullptr};
    spine::AnimationState* state_{nullptr};
};

} // namespace HIKARI
