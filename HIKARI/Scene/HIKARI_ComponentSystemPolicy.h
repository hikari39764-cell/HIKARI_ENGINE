#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace HIKARI {

    class SystemScheduler;
    class SystemTypeRegistry;
    struct SceneDocument;

    struct ComponentSystemRule {
        std::string componentType{};
        std::string systemId{};
        int defaultExecutionOrder = 0;
    };

    struct ComponentSystemInstallResult {
        bool success = true;
        std::vector<std::string> installedSystems{};
        std::vector<std::string> unavailableSystems{};
    };

    class ComponentSystemPolicy {
    public:
        bool Register(ComponentSystemRule rule);
        void Clear() noexcept;

        bool IsComponentDrivenSystem(
            std::string_view systemId) const noexcept;
        ComponentSystemInstallResult InstallRequiredSystems(
            const SceneDocument& document,
            const SystemTypeRegistry& typeRegistry,
            SystemScheduler& scheduler) const;

        const std::vector<ComponentSystemRule>& GetRules() const noexcept {
            return rules_;
        }

    private:
        std::vector<ComponentSystemRule> rules_{};
    };

} // namespace HIKARI
