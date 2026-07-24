#pragma once

#include "Editor/Assets/Workspace/HIKARI_ResourceWorkspacePanel.h"

namespace HIKARI {
struct AssetImportBatchStatus;
}

namespace HIKARI::EDITOR::RESOURCE_WORKSPACE {

#if defined(HIKARI_WITH_EDITOR)
ResourceImportBatchMonitor
MakeImportMonitor(const AssetImportBatchStatus &status);

void DrawScopeCombo(const AssetDatabase &assetDatabase,
                    const AssetUsageSummary &usageSummary,
                    AssetBrowserScope &activeScope);

void DrawPreviewAndImportLog(AssetDatabase &assetDatabase,
                             EditorSelection &selection);
#endif

} // namespace HIKARI::EDITOR::RESOURCE_WORKSPACE
