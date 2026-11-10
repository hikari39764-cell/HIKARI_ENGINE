#include "Editor/Project/HIKARI_ProjectModuleList.h"

#include <algorithm>
#include <span>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

ProjectModuleList BuildProjectModuleList(const DocumentSceneBase& scene) {
    ProjectModuleList list{};
    const std::filesystem::path& projectRoot = scene.GetAssetDatabase().GetProjectRoot();
    ProjectDescriptor descriptor{};
    if (!LoadProjectDescriptor(projectRoot / kProjectDescriptorFilename, descriptor, &list.error)) {
        return list;
    }
    const std::span<const InstalledModuleStatus> installed = scene.GetInstalledModules();
    for (ProjectModuleOption& option : ListProjectModuleOptions(descriptor, projectRoot, GetEngineModulesDirectory())) {
        ProjectModuleRow row{};
        const auto status =
            std::find_if(installed.begin(), installed.end(), [&option](const InstalledModuleStatus& item) {
                return item.manifest.id == option.manifest.id;
            });
        row.installed = status != installed.end() ? &*status : nullptr;
        // 使えない包は開き直しても読み込まれないので、食い違いには数えない。
        const bool wanted = option.enabled || !option.requiredBy.empty();
        row.reopenPending = option.problem.empty() && wanted != (row.installed != nullptr);
        row.option = std::move(option);
        list.rows.push_back(std::move(row));
    }
    return list;
}

} // namespace HIKARI::EDITOR
