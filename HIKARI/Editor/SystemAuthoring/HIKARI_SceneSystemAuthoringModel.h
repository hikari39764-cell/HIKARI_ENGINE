#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
    class ComponentSystemPolicy;
    class SystemScheduler;
    class SystemTypeRegistry;
    struct SystemTypeInfo;
}

namespace HIKARI::EDITOR {

    struct SceneSystemAuthoringRow {
        std::string systemId{};
        const SystemTypeInfo* runtimeInfo = nullptr;
        size_t documentIndex = 0;
        bool hasDocumentEntry = false;
        bool projectDefault = false;
        bool componentDriven = false;
        bool scheduled = false;
        size_t requiredComponentCount = 0;
        int defaultExecutionOrder = 0;
        std::vector<std::string> requiredComponentTypes{};
    };

    std::vector<SceneSystemAuthoringRow>
        BuildSceneSystemAuthoringRows(
            const SceneDocument& document,
            const SystemTypeRegistry& typeRegistry,
            const ComponentSystemPolicy& policy,
            const SystemScheduler& scheduler,
            const std::vector<SceneSystemData>& defaultSceneSystems);

    bool MatchesSceneSystemFilter(
        const SceneSystemAuthoringRow& row,
        std::string_view filter);
    SceneSystemData MakeEditableSceneSystemEntry(
        const SceneSystemAuthoringRow& row,
        const SceneDocument& document,
        const SystemTypeRegistry& typeRegistry);
    bool IsSceneSystemEffectivelyEnabled(
        const SceneSystemAuthoringRow& row,
        const SceneDocument& document);
    bool IsSceneSystemInstalled(
        const SceneSystemAuthoringRow& row) noexcept;
    bool IsSceneSystemAutomatic(
        const SceneSystemAuthoringRow& row) noexcept;
    bool HasSceneSystemIssue(
        const SceneSystemAuthoringRow& row,
        const SceneDocument& document);
    void UpsertSceneSystem(
        SceneDocument& document,
        SceneSystemData system);
    void RemoveSceneSystemOverride(
        SceneDocument& document,
        std::string_view systemId);

} // namespace HIKARI::EDITOR
