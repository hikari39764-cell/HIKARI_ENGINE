#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Project/HIKARI_ProjectSettings.h"

namespace HIKARI {

    class DocumentSceneBase;
    class RuntimeFeatureCatalog;

    class ProjectFeaturesPanel {
    public:
        void Draw(DocumentSceneBase& scene);

    private:
        void EnsureLoaded(const std::filesystem::path& projectRoot);
        void ReloadDraft(const std::filesystem::path& projectRoot);
        void NormalizeDraft(const RuntimeFeatureCatalog& catalog);

        std::filesystem::path loadedProjectRoot_{};
        ProjectSettingsService settingsService_{};
        std::vector<std::string> draftFeatureIds_{};
        std::string statusMessage_{};
        bool statusIsError_ = false;
        bool dirty_ = false;
        bool draftNeedsNormalization_ = false;
    };

} // namespace HIKARI
