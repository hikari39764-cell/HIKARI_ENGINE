#pragma once

#include <string>
#include <vector>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
    class DocumentSceneBase;
    class SystemTypeRegistry;
}

namespace HIKARI::EDITOR {

    class EditorToolHost;
    class SystemAuthoringRegistry;
    struct SceneSystemAuthoringRow;

    enum class SystemSettingsInspectorAction {
        None,
        Apply,
        RemoveOverride,
    };

    struct SystemSettingsInspectorResult {
        SystemSettingsInspectorAction action =
            SystemSettingsInspectorAction::None;
        SceneSystemData system{};
    };

    class SystemSettingsInspector {
    public:
        void Select(
            SceneSystemData initial,
            std::string displayName);
        void Clear();
        void Revert();

        bool HasTarget() const noexcept;
        bool IsDirty() const noexcept;
        const std::string& GetTargetSystemId() const noexcept;

        SystemSettingsInspectorResult Draw(
            const SceneSystemAuthoringRow& row,
            DocumentSceneBase& scene,
            const SystemTypeRegistry& runtimeRegistry,
            const SystemAuthoringRegistry& authoringRegistry,
            EditorToolHost& toolHost,
            bool readOnly);

    private:
        bool hasTarget_ = false;
        SceneSystemData original_{};
        SceneSystemData draft_{};
        std::string displayName_{};
        std::vector<std::string> validationIssues_{};
    };

} // namespace HIKARI::EDITOR
