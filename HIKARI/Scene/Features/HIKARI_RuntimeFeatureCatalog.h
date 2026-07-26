#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Runtime/Project/HIKARI_GameProjectSdk.h"

namespace HIKARI {

    struct RuntimeFeatureInstallReport {
        bool success = true;
        struct Resolution {
            bool success = true;
            std::vector<std::string> resolvedFeatures{};
            std::vector<std::string> implicitlyEnabledFeatures{};
            std::vector<std::string> unknownFeatures{};
            std::vector<std::string> issues{};
        } resolution{};
        std::vector<std::string> installedFeatures{};
        std::vector<std::string> failedFeatures{};
    };

    struct RuntimeFeatureInfo {
        std::string featureId{};
        std::string displayName{};
        std::string description{};
        std::vector<std::string> requiredFeatureIds{};
        std::vector<std::string> componentTypeNames{};
        std::vector<std::string> systemIds{};
        bool active = false;
    };

    enum class RuntimeFeatureSceneIssueKind {
        DisabledComponent,
        DisabledSystem,
    };

    struct RuntimeFeatureSceneIssue {
        RuntimeFeatureSceneIssueKind kind =
            RuntimeFeatureSceneIssueKind::DisabledComponent;
        std::string featureId{};
        std::string itemId{};
        SceneObjectId objectId{};
        std::string objectName{};
    };

    class RuntimeFeatureCatalog {
    public:
        using RuntimeFeatureDestroyFn =
            void (*)(IRuntimeFeature*) noexcept;

        RuntimeFeatureCatalog() = default;
        ~RuntimeFeatureCatalog();

        RuntimeFeatureCatalog(const RuntimeFeatureCatalog&) = delete;
        RuntimeFeatureCatalog& operator=(const RuntimeFeatureCatalog&) = delete;
        RuntimeFeatureCatalog(RuntimeFeatureCatalog&&) noexcept;
        RuntimeFeatureCatalog& operator=(RuntimeFeatureCatalog&&) noexcept;

        bool Add(std::unique_ptr<IRuntimeFeature> feature);
        template<class Feature, class... Arguments>
        bool AddModuleOwned(Arguments&&... arguments) {
            static_assert(
                std::is_base_of_v<IRuntimeFeature, Feature>);
            auto feature = std::make_unique<Feature>(
                std::forward<Arguments>(arguments)...);
            IRuntimeFeature* value = feature.get();
            if (!AddModuleOwned(
                    value,
                    [](IRuntimeFeature* owned) noexcept {
                        delete static_cast<Feature*>(owned);
                    })) {
                return false;
            }
            feature.release();
            return true;
        }
        HIKARI_GAME_PROJECT_SDK_API bool AddModuleOwned(
            IRuntimeFeature* feature,
            RuntimeFeatureDestroyFn destroy);
        void Clear() noexcept;
        RuntimeFeatureInstallReport RegisterAll(
            RuntimeFeatureContext& context);
        RuntimeFeatureInstallReport RegisterEnabled(
            RuntimeFeatureContext& context,
            const std::vector<std::string>& enabledFeatureIds);
        RuntimeFeatureInstallReport::Resolution ResolveEnabledFeatures(
            const std::vector<std::string>& enabledFeatureIds) const;
        std::vector<SceneSystemData> CreateDefaultSceneSystems() const;

        std::vector<std::string> GetFeatureIds() const;
        std::vector<std::string> GetActiveFeatureIds() const;
        std::vector<RuntimeFeatureInfo> GetFeatureInfos() const;
        bool IsFeatureActive(std::string_view featureId) const noexcept;
        std::string FindOwningFeatureForComponent(
            std::string_view componentType) const;
        std::string FindOwningFeatureForSystem(
            std::string_view systemId) const;
        std::vector<RuntimeFeatureSceneIssue> AnalyzeSceneDocument(
            const SceneDocument& document) const;

    private:
        struct FeatureEntry {
            IRuntimeFeature* feature = nullptr;
            RuntimeFeatureDestroyFn destroy = nullptr;
        };

        bool AddWithDestroy(
            IRuntimeFeature* feature,
            RuntimeFeatureDestroyFn destroy);
        IRuntimeFeature* FindFeatureMutable(
            std::string_view featureId) noexcept;
        const IRuntimeFeature* FindFeature(
            std::string_view featureId) const noexcept;
        std::vector<FeatureEntry> features_{};
        std::vector<std::string> activeFeatureIds_{};
        bool selectionResolved_ = false;
    };

} // namespace HIKARI
