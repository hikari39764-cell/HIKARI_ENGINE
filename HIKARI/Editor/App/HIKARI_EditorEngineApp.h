#pragma once

#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Runtime/App/HIKARI_IHostApplication.h"
#include "Runtime/App/HIKARI_RuntimeEngineApp.h"

namespace HIKARI::EDITOR {

    class EditorEngineApp final : public RUNTIME::IHostApplication {
    public:
        EditorEngineApp() = default;

        bool Initialize() override;
        void Update(float deltaTime) override;
        void Render() override;
        void RenderHostUi() override;
        void UpdateHostSessions() override;
        void RequestHostStop() override;
        bool IsWindowedHostSessionRunning() const override;
        bool IsHostTransitioning() const override;
        void Shutdown() override;

    private:
        RUNTIME::RuntimeEngineApp runtimeApp_;
        DocumentSceneEditorController documentSceneEditorController_{};
        EditorPlaySession editorPlaySession_{};
    };

} // namespace HIKARI::EDITOR
