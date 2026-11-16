#pragma once
#include <memory>
#include <string>
#include "Render2D/HIKARI_Transform2D.h"
#include "Render2D/HIKARI_Renderer.h"

#include <spine/Atlas.h>
#include <spine/Skeleton.h>
#include <spine/SkeletonData.h>
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

    bool Load(const std::string& atlasPath, const std::string& jsonPath);
    void SetAnimation(const char* name, bool loop, int trackIndex = 0, float mixDuration = -1.0f,
                      bool forceSet = false);

    void Update(float dt, bool usePhysics = false);
    void Draw();

    bool IsAnimationFinished(const char* name, int trackIndex = 0);

private:
    // 後ろのメンバーほど前のメンバーを参照する。破棄は宣言の逆順なので、参照する側から先に消える。
    spine::SkeletonRenderer renderer_;
    std::unique_ptr<spine::Atlas> atlas_;
    std::unique_ptr<spine::SkeletonData> skelData_;
    std::unique_ptr<spine::Skeleton> skeleton_;
    std::unique_ptr<spine::AnimationStateData> stateData_;
    std::unique_ptr<spine::AnimationState> state_;
};

} // namespace HIKARI
