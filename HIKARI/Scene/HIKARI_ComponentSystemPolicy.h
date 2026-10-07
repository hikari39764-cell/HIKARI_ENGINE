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

    ComponentSystemInstallResult InstallRequiredSystems(const SceneDocument& document,
                                                        const SystemTypeRegistry& typeRegistry,
                                                        SystemScheduler& scheduler) const;
    // 指定コンポーネントを場景へ足すと、まだ scheduler に無いシステムの導入が必要になるか。
    bool NeedsSystemInstall(std::string_view componentType, const SceneDocument& document,
                            const SystemScheduler& scheduler) const;

    const std::vector<ComponentSystemRule>& GetRules() const noexcept { return rules_; }

private:
    std::vector<ComponentSystemRule> rules_{};
};

} // namespace HIKARI
