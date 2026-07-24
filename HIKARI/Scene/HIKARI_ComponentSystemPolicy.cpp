#include "Scene/HIKARI_ComponentSystemPolicy.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {

    namespace {
        bool SceneUsesComponent(
            const SceneDocument& document,
            std::string_view componentType) {

            return std::any_of(
                document.objects.begin(),
                document.objects.end(),
                [componentType](const SceneObjectData& object) {
                    return std::any_of(
                        object.components.begin(),
                        object.components.end(),
                        [componentType](
                            const SceneComponentData& component) {
                            return component.type == componentType;
                        });
                });
        }

        const SceneSystemData* FindSystemSettings(
            const SceneDocument& document,
            std::string_view systemId) noexcept {

            const auto found = std::find_if(
                document.systems.begin(),
                document.systems.end(),
                [systemId](const SceneSystemData& system) {
                    return system.systemId == systemId;
                });
            return found != document.systems.end() ? &*found : nullptr;
        }
    }

    bool ComponentSystemPolicy::Register(ComponentSystemRule rule) {
        if (rule.componentType.empty() || rule.systemId.empty()) {
            return false;
        }
        const bool duplicate = std::any_of(
            rules_.begin(),
            rules_.end(),
            [&rule](const ComponentSystemRule& existing) {
                return existing.componentType == rule.componentType &&
                    existing.systemId == rule.systemId;
            });
        if (duplicate) {
            return false;
        }
        rules_.push_back(std::move(rule));
        return true;
    }

    void ComponentSystemPolicy::Clear() noexcept {
        rules_.clear();
    }

    bool ComponentSystemPolicy::IsComponentDrivenSystem(
        std::string_view systemId) const noexcept {

        return std::any_of(
            rules_.begin(),
            rules_.end(),
            [systemId](const ComponentSystemRule& rule) {
                return systemId == rule.systemId;
            });
    }

    ComponentSystemInstallResult ComponentSystemPolicy::InstallRequiredSystems(
        const SceneDocument& document,
        const SystemTypeRegistry& typeRegistry,
        SystemScheduler& scheduler) const {

        ComponentSystemInstallResult result{};
        for (const ComponentSystemRule& rule : rules_) {
            if (!SceneUsesComponent(document, rule.componentType) ||
                scheduler.HasSystem(rule.systemId)) {
                continue;
            }

            const SceneSystemData* settings = FindSystemSettings(
                document,
                rule.systemId);
            if (settings != nullptr && !settings->enabled) {
                continue;
            }
            const nlohmann::json systemSettings = settings != nullptr
                ? settings->settings
                : nlohmann::json::object();
            std::unique_ptr<ISystem> system = typeRegistry.Create(
                rule.systemId,
                systemSettings);
            if (!system) {
                result.success = false;
                result.unavailableSystems.emplace_back(rule.systemId);
                continue;
            }

            const int executionOrder = settings != nullptr
                ? settings->executionOrder
                : rule.defaultExecutionOrder;
            if (!scheduler.AddSystem(
                    rule.systemId,
                    executionOrder,
                    std::move(system))) {
                result.success = false;
                result.unavailableSystems.emplace_back(rule.systemId);
                continue;
            }
            result.installedSystems.emplace_back(rule.systemId);
        }
        return result;
    }

} // namespace HIKARI
