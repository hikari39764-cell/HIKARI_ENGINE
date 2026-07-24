#pragma once

#include "Editor/Authoring/HIKARI_CollisionAuthoringService.h"

namespace HIKARI::EDITOR {

    class CollisionAuthoringDialog {
    public:
        void Open(SceneObjectId selectedObject);
        bool Draw(CollisionAuthoringRequest& outRequest);

    private:
        SceneObjectId selectedObject_{};
        int scope_ = 0;
        int shapeSource_ = 0;
        int bodyMode_ = 0;
        int existingColliderPolicy_ = 0;
        bool makeTrigger_ = false;
        bool openRequested_ = false;
    };

} // namespace HIKARI::EDITOR
