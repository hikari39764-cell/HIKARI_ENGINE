#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Editor/Project/HIKARI_ProjectModuleList.h"
#include "Project/HIKARI_ProjectSettings.h"

namespace HIKARI {

class DocumentSceneBase;
class RuntimeFeatureCatalog;

class ProjectFeaturesPanel {
public:
    void Draw(DocumentSceneBase& scene);
    bool IsDirty() const noexcept { return dirty_; }

private:
    void EnsureLoaded(const std::filesystem::path& projectRoot);
    void ReloadDraft(const std::filesystem::path& projectRoot);
    void NormalizeDraft(const RuntimeFeatureCatalog& catalog);
    // 挿件包の一覧。チェックを変えるとすぐ Project.hikari へ書く（反映は項目を開き直した時）。
    void DrawModules(DocumentSceneBase& scene);

    std::filesystem::path loadedProjectRoot_{};
    ProjectSettingsService settingsService_{};
    std::vector<std::string> draftFeatureIds_{};
    std::string statusMessage_{};
    bool statusIsError_ = false;
    bool dirty_ = false;
    bool draftNeedsNormalization_ = false;
    // フォルダと清単を毎フレーム読まないよう持っておく。項目が変わった時、変更した時、「更新」で読み直す。
    EDITOR::ProjectModuleList moduleList_{};
    std::string moduleMessage_{};
    bool moduleListStale_ = true;
};

} // namespace HIKARI
