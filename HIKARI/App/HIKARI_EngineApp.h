#pragma once
#include <memory>

#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneManager.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#endif

namespace HIKARI {

    class EngineApp {
    public:
        EngineApp();
        bool Initialize();
        void Update(float dt);
        void Render();
        void RenderImGui();
        void UpdatePlaySessions();
        void RequestPlayStop();
        bool IsWindowedPlayRunning() const;
        bool IsPlayTransitioning() const;
        void Shutdown();

    private:
        SceneManager sceneManager_{};
        SceneTransitionBus sceneTransitionBus_;
#if defined(HIKARI_WITH_EDITOR)
        DocumentSceneEditorController documentSceneEditorController_{};
        EDITOR::EditorPlaySession editorPlaySession_{};
#endif
    };

} // namespace HIKARI
