#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Scene/Features/HIKARI_RuntimeFeature.h"

namespace HIKARI {

    RuntimeFeatureCatalog::~RuntimeFeatureCatalog() = default;
    RuntimeFeatureCatalog::RuntimeFeatureCatalog(
        RuntimeFeatureCatalog&&) noexcept = default;
    RuntimeFeatureCatalog& RuntimeFeatureCatalog::operator=(
        RuntimeFeatureCatalog&&) noexcept = default;

    bool RuntimeFeatureCatalog::Add(
        std::unique_ptr<IRuntimeFeature> feature) {

        if (!feature || feature->GetFeatureId().empty()) {
            return false;
        }
        const std::string_view featureId = feature->GetFeatureId();
        const bool duplicate = std::any_of(
            features_.begin(),
            features_.end(),
            [featureId](const std::unique_ptr<IRuntimeFeature>& existing) {
                return existing && existing->GetFeatureId() == featureId;
            });
        if (duplicate) {
            return false;
        }
        features_.push_back(std::move(feature));
        return true;
    }

    void RuntimeFeatureCatalog::Clear() noexcept {
        features_.clear();
        activeFeatureIds_.clear();
        selectionResolved_ = false;
    }

    RuntimeFeatureInstallReport RuntimeFeatureCatalog::RegisterAll(
        RuntimeFeatureContext& context) {

        return RegisterEnabled(context, GetFeatureIds());
    }

    RuntimeFeatureInstallReport::Resolution
        RuntimeFeatureCatalog::ResolveEnabledFeatures(
            const std::vector<std::string>& enabledFeatureIds) const {

        RuntimeFeatureInstallReport::Resolution resolution{};
        std::unordered_set<std::string> requested;
        for (const std::string& featureId : enabledFeatureIds) {
            if (!featureId.empty()) {
                requested.insert(featureId);
            }
        }

        enum class VisitState : uint8_t {
            Visiting,
            Resolved,
            Failed,
        };
        std::unordered_map<std::string, VisitState> states;
        std::unordered_set<std::string> implicit;
        std::unordered_set<std::string> issueKeys;

        auto addIssue = [&resolution, &issueKeys](
            const std::string& key,
            std::string message) {
            if (issueKeys.insert(key).second) {
                resolution.issues.push_back(std::move(message));
            }
            resolution.success = false;
        };

        std::function<bool(std::string_view)> visit;
        visit = [&](std::string_view featureId) -> bool {
            const std::string id(featureId);
            const auto stateIt = states.find(id);
            if (stateIt != states.end()) {
                if (stateIt->second == VisitState::Resolved) {
                    return true;
                }
                if (stateIt->second == VisitState::Visiting) {
                    addIssue(
                        "cycle:" + id,
                        "Cyclic runtime feature dependency: " + id);
                }
                return false;
            }

            const IRuntimeFeature* feature = FindFeature(id);
            if (feature == nullptr) {
                if (std::find(
                        resolution.unknownFeatures.begin(),
                        resolution.unknownFeatures.end(),
                        id) == resolution.unknownFeatures.end()) {
                    resolution.unknownFeatures.push_back(id);
                }
                addIssue(
                    "unknown:" + id,
                    "Unknown runtime feature: " + id);
                states.emplace(id, VisitState::Failed);
                return false;
            }

            states.emplace(id, VisitState::Visiting);
            bool dependenciesResolved = true;
            for (const std::string_view dependency :
                    feature->GetRequiredFeatureIds()) {
                if (requested.find(std::string(dependency)) ==
                    requested.end()) {
                    implicit.insert(std::string(dependency));
                }
                if (!visit(dependency)) {
                    dependenciesResolved = false;
                    addIssue(
                        "dependency:" + id + ":" +
                            std::string(dependency),
                        "Runtime feature " + id +
                            " requires unavailable feature " +
                            std::string(dependency));
                }
            }

            states[id] = dependenciesResolved
                ? VisitState::Resolved
                : VisitState::Failed;
            if (dependenciesResolved) {
                resolution.resolvedFeatures.push_back(id);
            }
            return dependenciesResolved;
        };

        for (const std::string& featureId : enabledFeatureIds) {
            if (!featureId.empty()) {
                (void)visit(featureId);
            }
        }
        for (const std::string& featureId :
                resolution.resolvedFeatures) {
            if (implicit.find(featureId) != implicit.end()) {
                resolution.implicitlyEnabledFeatures.push_back(featureId);
            }
        }
        return resolution;
    }

    RuntimeFeatureInstallReport RuntimeFeatureCatalog::RegisterEnabled(
        RuntimeFeatureContext& context,
        const std::vector<std::string>& enabledFeatureIds) {

        RuntimeFeatureInstallReport report{};
        report.resolution = ResolveEnabledFeatures(enabledFeatureIds);
        report.success = report.resolution.success;
        activeFeatureIds_.clear();
        selectionResolved_ = true;

        for (const std::string& featureId :
                report.resolution.resolvedFeatures) {
            IRuntimeFeature* feature = FindFeatureMutable(featureId);
            bool dependenciesInstalled = feature != nullptr;
            if (feature != nullptr) {
                for (const std::string_view dependency :
                        feature->GetRequiredFeatureIds()) {
                    if (!IsFeatureActive(dependency)) {
                        dependenciesInstalled = false;
                        break;
                    }
                }
            }
            if (dependenciesInstalled && feature->Register(context)) {
                report.installedFeatures.push_back(featureId);
                activeFeatureIds_.push_back(featureId);
                continue;
            }
            report.success = false;
            report.failedFeatures.push_back(featureId);
        }
        return report;
    }

    std::vector<SceneSystemData>
        RuntimeFeatureCatalog::CreateDefaultSceneSystems() const {
        std::vector<SceneSystemData> systems;
        for (const std::unique_ptr<IRuntimeFeature>& feature : features_) {
            if (feature &&
                (!selectionResolved_ ||
                    IsFeatureActive(feature->GetFeatureId()))) {
                feature->AppendDefaultSceneSystems(systems);
            }
        }
        std::vector<SceneSystemData> uniqueSystems;
        uniqueSystems.reserve(systems.size());
        for (SceneSystemData& system : systems) {
            if (system.systemId.empty()) {
                continue;
            }
            const bool duplicate = std::any_of(
                uniqueSystems.begin(),
                uniqueSystems.end(),
                [&system](const SceneSystemData& existing) {
                    return existing.systemId == system.systemId;
                });
            if (!duplicate) {
                uniqueSystems.push_back(std::move(system));
            }
        }
        return uniqueSystems;
    }

    std::vector<std::string> RuntimeFeatureCatalog::GetFeatureIds() const {
        std::vector<std::string> ids;
        ids.reserve(features_.size());
        for (const std::unique_ptr<IRuntimeFeature>& feature : features_) {
            if (feature) {
                ids.emplace_back(feature->GetFeatureId());
            }
        }
        return ids;
    }

    std::vector<std::string>
        RuntimeFeatureCatalog::GetActiveFeatureIds() const {
        return activeFeatureIds_;
    }

    std::vector<RuntimeFeatureInfo>
        RuntimeFeatureCatalog::GetFeatureInfos() const {
        std::vector<RuntimeFeatureInfo> infos;
        infos.reserve(features_.size());
        for (const std::unique_ptr<IRuntimeFeature>& feature : features_) {
            if (!feature) {
                continue;
            }
            RuntimeFeatureInfo info{};
            info.featureId = feature->GetFeatureId();
            info.displayName = feature->GetDisplayName();
            info.description = feature->GetDescription();
            for (const std::string_view dependency :
                    feature->GetRequiredFeatureIds()) {
                info.requiredFeatureIds.emplace_back(dependency);
            }
            for (const std::string_view componentType :
                    feature->GetComponentTypeNames()) {
                info.componentTypeNames.emplace_back(componentType);
            }
            for (const std::string_view systemId :
                    feature->GetSystemIds()) {
                info.systemIds.emplace_back(systemId);
            }
            info.active = IsFeatureActive(info.featureId);
            infos.push_back(std::move(info));
        }
        return infos;
    }

    bool RuntimeFeatureCatalog::IsFeatureActive(
        std::string_view featureId) const noexcept {
        if (!selectionResolved_) {
            return FindFeature(featureId) != nullptr;
        }
        return std::find(
            activeFeatureIds_.begin(),
            activeFeatureIds_.end(),
            featureId) != activeFeatureIds_.end();
    }

    std::string RuntimeFeatureCatalog::FindOwningFeatureForComponent(
        std::string_view componentType) const {
        for (const std::unique_ptr<IRuntimeFeature>& feature : features_) {
            if (!feature) {
                continue;
            }
            for (const std::string_view ownedType :
                    feature->GetComponentTypeNames()) {
                if (ownedType == componentType) {
                    return std::string(feature->GetFeatureId());
                }
            }
        }
        return {};
    }

    std::string RuntimeFeatureCatalog::FindOwningFeatureForSystem(
        std::string_view systemId) const {
        for (const std::unique_ptr<IRuntimeFeature>& feature : features_) {
            if (!feature) {
                continue;
            }
            for (const std::string_view ownedId : feature->GetSystemIds()) {
                if (ownedId == systemId) {
                    return std::string(feature->GetFeatureId());
                }
            }
        }
        return {};
    }

    std::vector<RuntimeFeatureSceneIssue>
        RuntimeFeatureCatalog::AnalyzeSceneDocument(
            const SceneDocument& document) const {
        std::vector<RuntimeFeatureSceneIssue> issues;
        for (const SceneObjectData& object : document.objects) {
            for (const SceneComponentData& component : object.components) {
                const std::string owner =
                    FindOwningFeatureForComponent(component.type);
                if (!owner.empty() && !IsFeatureActive(owner)) {
                    issues.push_back(RuntimeFeatureSceneIssue{
                        RuntimeFeatureSceneIssueKind::DisabledComponent,
                        owner,
                        component.type,
                        object.id,
                        object.name
                    });
                }
            }
        }
        for (const SceneSystemData& system : document.systems) {
            const std::string owner =
                FindOwningFeatureForSystem(system.systemId);
            if (!owner.empty() && !IsFeatureActive(owner)) {
                issues.push_back(RuntimeFeatureSceneIssue{
                    RuntimeFeatureSceneIssueKind::DisabledSystem,
                    owner,
                    system.systemId,
                    {},
                    {}
                });
            }
        }
        return issues;
    }

    const IRuntimeFeature* RuntimeFeatureCatalog::FindFeature(
        std::string_view featureId) const noexcept {
        const auto found = std::find_if(
            features_.begin(),
            features_.end(),
            [featureId](const std::unique_ptr<IRuntimeFeature>& feature) {
                return feature && feature->GetFeatureId() == featureId;
            });
        return found != features_.end() ? found->get() : nullptr;
    }

    IRuntimeFeature* RuntimeFeatureCatalog::FindFeatureMutable(
        std::string_view featureId) noexcept {
        const auto found = std::find_if(
            features_.begin(),
            features_.end(),
            [featureId](const std::unique_ptr<IRuntimeFeature>& feature) {
                return feature && feature->GetFeatureId() == featureId;
            });
        return found != features_.end() ? found->get() : nullptr;
    }

} // namespace HIKARI
