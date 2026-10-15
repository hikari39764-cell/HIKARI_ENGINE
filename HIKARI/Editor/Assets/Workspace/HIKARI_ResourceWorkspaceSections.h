#pragma once

#include "Editor/Assets/Workspace/HIKARI_ResourceWorkspacePanel.h"

namespace HIKARI {
struct AssetImportBatchStatus;
}

namespace HIKARI::EDITOR::RESOURCE_WORKSPACE {

ResourceImportBatchMonitor MakeImportMonitor(const AssetImportBatchStatus& status);

void DrawScopeCombo(const ASSET_BROWSER::AssetBrowserStatisticsCache& statistics, const AssetUsageSummary& usageSummary,
                    AssetBrowserScope& activeScope);

void DrawPreviewAndImportLog(AssetDatabase& assetDatabase, EditorSelection& selection);

} // namespace HIKARI::EDITOR::RESOURCE_WORKSPACE
