#pragma once

#include <string>

#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI {

    class GameDocumentScene : public DocumentSceneBase {
    public:
        explicit GameDocumentScene(std::string sceneId);

        void OnEnter() override;
        const char* GetSceneName() const override;
        void RenderImGui() override;

    };

} // namespace HIKARI
