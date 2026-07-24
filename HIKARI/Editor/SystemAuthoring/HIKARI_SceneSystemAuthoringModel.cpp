#include "Editor/SystemAuthoring/HIKARI_SceneSystemAuthoringModel.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <utility>

#include "Core/Text/HIKARI_AsciiCase.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI::EDITOR {
    namespace {

        size_t CountSceneComponents(
            const SceneDocument& document,
            std::string_view componentType) {

            size_t count = 0;
            for (const SceneObjectData& object : document.objects) {
                count += static_cast<size_t>(std::count_if(
                    object.components.begin(),
                    object.components.end(),
                    [componentType](const SceneComponentData& component) {
                        return component.type == componentType;
                    }));
            }
            return count;
        }

    }

    std::vector<SceneSystemAuthoringRow>
        BuildSceneSystemAuthoringRows(
            const SceneDocument& document,
            const SystemTypeRegistry& typeRegistry,
            const ComponentSystemPolicy& policy,
            const SystemScheduler& scheduler,
            const std::vector<SceneSystemData>& defaultSceneSystems) {

        std::vector<std::string> systemIds =
            typeRegistry.GetTypeNames();
        std::unordered_set<std::string> knownIds(
            systemIds.begin(), systemIds.end());
        for (const SceneSystemData& entry : document.systems) {
            if (knownIds.insert(entry.systemId).second) {
                systemIds.push_back(entry.systemId);
            }
        }
        for (const SceneSystemData& entry : defaultSceneSystems) {
            if (knownIds.insert(entry.systemId).second) {
                systemIds.push_back(entry.systemId);
            }
        }
        std::sort(systemIds.begin(), systemIds.end());

        std::vector<SceneSystemAuthoringRow> rows;
        rows.reserve(systemIds.size());
        for (const std::string& systemId : systemIds) {
            SceneSystemAuthoringRow row{};
            row.systemId = systemId;
            row.runtimeInfo = typeRegistry.Find(systemId);
            row.scheduled = scheduler.HasSystem(systemId);

            const auto defaultEntry = std::find_if(
                defaultSceneSystems.begin(),
                defaultSceneSystems.end(),
                [&systemId](const SceneSystemData& entry) {
                    return entry.systemId == systemId;
                });
            if (defaultEntry != defaultSceneSystems.end()) {
                row.projectDefault = true;
                row.defaultExecutionOrder =
                    defaultEntry->executionOrder;
            }

            const auto documentEntry = std::find_if(
                document.systems.begin(),
                document.systems.end(),
                [&systemId](const SceneSystemData& entry) {
                    return entry.systemId == systemId;
                });
            row.hasDocumentEntry =
                documentEntry != document.systems.end();
            if (row.hasDocumentEntry) {
                row.documentIndex = static_cast<size_t>(
                    std::distance(document.systems.begin(), documentEntry));
            }

            for (const ComponentSystemRule& rule : policy.GetRules()) {
                if (rule.systemId != systemId) {
                    continue;
                }
                row.componentDriven = true;
                if (row.requiredComponentTypes.empty()) {
                    row.defaultExecutionOrder =
                        rule.defaultExecutionOrder;
                }
                row.requiredComponentCount += CountSceneComponents(
                    document,
                    rule.componentType);
                row.requiredComponentTypes.push_back(
                    rule.componentType);
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

    bool MatchesSceneSystemFilter(
        const SceneSystemAuthoringRow& row,
        std::string_view filter) {

        if (filter.empty()) {
            return true;
        }
        const std::string query = TEXT::ToLowerAsciiCopy(filter);
        std::string searchable = row.systemId;
        if (row.runtimeInfo) {
            searchable += " ";
            searchable += row.runtimeInfo->displayName;
            searchable += " ";
            searchable += row.runtimeInfo->featureId;
        }
        return TEXT::ToLowerAsciiCopy(searchable).find(query) !=
            std::string::npos;
    }

    SceneSystemData MakeEditableSceneSystemEntry(
        const SceneSystemAuthoringRow& row,
        const SceneDocument& document,
        const SystemTypeRegistry& typeRegistry) {

        if (row.hasDocumentEntry &&
            row.documentIndex < document.systems.size()) {
            return document.systems[row.documentIndex];
        }
        SceneSystemData entry{};
        entry.systemId = row.systemId;
        entry.enabled = true;
        entry.executionOrder = row.defaultExecutionOrder;
        entry.settings = typeRegistry.CreateDefaultSettings(
            row.systemId);
        return entry;
    }

    bool IsSceneSystemEffectivelyEnabled(
        const SceneSystemAuthoringRow& row,
        const SceneDocument& document) {

        if (row.hasDocumentEntry &&
            row.documentIndex < document.systems.size()) {
            return document.systems[row.documentIndex].enabled;
        }
        return row.componentDriven && row.requiredComponentCount > 0;
    }

    bool IsSceneSystemInstalled(
        const SceneSystemAuthoringRow& row) noexcept {

        return row.hasDocumentEntry || row.requiredComponentCount > 0;
    }

    bool IsSceneSystemAutomatic(
        const SceneSystemAuthoringRow& row) noexcept {

        return row.componentDriven &&
            row.requiredComponentCount > 0 &&
            !row.hasDocumentEntry;
    }

    bool HasSceneSystemIssue(
        const SceneSystemAuthoringRow& row,
        const SceneDocument& document) {

        if (!IsSceneSystemInstalled(row)) {
            return false;
        }
        if (!row.runtimeInfo) {
            return true;
        }
        return IsSceneSystemEffectivelyEnabled(row, document) &&
            !row.scheduled;
    }

    void UpsertSceneSystem(
        SceneDocument& document,
        SceneSystemData system) {

        const auto found = std::find_if(
            document.systems.begin(),
            document.systems.end(),
            [&system](const SceneSystemData& entry) {
                return entry.systemId == system.systemId;
            });
        if (found != document.systems.end()) {
            *found = std::move(system);
        } else {
            document.systems.push_back(std::move(system));
        }
    }

    void RemoveSceneSystemOverride(
        SceneDocument& document,
        std::string_view systemId) {

        std::erase_if(
            document.systems,
            [systemId](const SceneSystemData& entry) {
                return entry.systemId == systemId;
            });
    }

} // namespace HIKARI::EDITOR
