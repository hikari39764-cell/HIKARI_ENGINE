#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI {
class AssetRegistry;
struct EditorSelection;
} // namespace HIKARI

namespace HIKARI::EDITOR::ASSET_INSPECTOR {

#if defined(HIKARI_WITH_EDITOR)
std::string ToClusteredGeometryArtifactStateText(ClusteredGeometryArtifactState state);

void DrawClusteredGeometryArtifactInfo(const ClusteredGeometryArtifactInfo& info);

void DrawModelDiagnostics(const ClusteredGeometryArtifactInfo& info, const nlohmann::json& report);

void CopyToBuffer(const std::string& text, char* buffer, size_t bufferSize);

bool DrawMaterialAssetEditor(AssetDatabase& assetDatabase, AssetRegistry& assetRegistry, EditorSelection& selection,
                             AssetRecord& record, AssetGuid& outApplyRuntimeGuid,
                             PbrMaterialAssetData& outApplyRuntimeData, std::string& outRefreshRuntimeGuid);
#endif

} // namespace HIKARI::EDITOR::ASSET_INSPECTOR
