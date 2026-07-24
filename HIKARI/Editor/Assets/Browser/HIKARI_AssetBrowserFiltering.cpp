#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorAssetIcons.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Platform/HIKARI_Win32Window.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"

namespace HIKARI::EDITOR::ASSET_BROWSER {

AssetType TypeFromFilterIndex(int index) {
  switch (index) {
  case 1:
    return AssetType::Texture;
  case 2:
    return AssetType::Model;
  case 3:
    return AssetType::Scene;
  case 4:
    return AssetType::Sky;
  case 5:
    return AssetType::Material;
  case 6:
    return AssetType::VfxEffect;
  case 7:
    return AssetType::Sequence;
  case 8:
    return AssetType::AnimationStateMachine;
  default:
    return AssetType::Unknown;
  }
}

bool MatchesTypeFilter(const AssetRecord &record, int typeFilter) {
  if (typeFilter == 0) {
    return true;
  }
  return record.type == TypeFromFilterIndex(typeFilter);
}

bool MatchesStateFilter(const AssetRecord &record, int stateFilter) {
  if (stateFilter == 0) {
    return true;
  }

  const AssetImportState state = GetImportState(record);
  switch (stateFilter) {
  case 1:
    return state == AssetImportState::Imported;
  case 2:
    return state == AssetImportState::Outdated;
  case 3:
    return state == AssetImportState::MissingSource ||
           state == AssetImportState::MissingMeta ||
           state == AssetImportState::MissingArtifact;
  case 4:
    return state == AssetImportState::UnknownImporter ||
           state == AssetImportState::DuplicateGuid ||
           state == AssetImportState::ImportFailed;
  case 5:
    return state == AssetImportState::MetaOnly;
  default:
    return true;
  }
}

bool MatchesSearch(const AssetRecord &record, const char *searchText) {
  if (!searchText || searchText[0] == '\0') {
    return true;
  }

  const std::string needle = TEXT::ToLowerAsciiCopy(searchText);
  const std::string haystack = TEXT::ToLowerAsciiCopy(
      record.displayName + " " + record.guid.value + " " +
      record.sourcePath.generic_string() + " " + record.meta.importerId);
  return haystack.find(needle) != std::string::npos;
}

nlohmann::json ReadImportSettings(const AssetRecord &record) {
  nlohmann::json settings =
      nlohmann::json::parse(record.meta.importSettingsJson, nullptr, false);
  return settings.is_object() ? settings : nlohmann::json::object();
}

bool MatchesScope(const AssetRecord &record,
                  const AssetUsageSummary *usageSummary,
                  AssetBrowserScope scope) {

  switch (scope) {
  case AssetBrowserScope::CurrentScene:
    return usageSummary && usageSummary->IsUsed(record.guid);
  case AssetBrowserScope::UnusedInScene:
    return usageSummary && !usageSummary->IsUsed(record.guid);
  case AssetBrowserScope::Broken:
    return IsBrokenAssetRecord(record);
  case AssetBrowserScope::Textures:
    return record.type == AssetType::Texture;
  case AssetBrowserScope::Models:
    return record.type == AssetType::Model;
  case AssetBrowserScope::Scenes:
    return record.type == AssetType::Scene;
  case AssetBrowserScope::Materials:
    return record.type == AssetType::Material;
  case AssetBrowserScope::Skies:
    return record.type == AssetType::Sky;
  case AssetBrowserScope::Vfx:
    return record.type == AssetType::VfxEffect;
  case AssetBrowserScope::Sequences:
    return record.type == AssetType::Sequence;
  case AssetBrowserScope::Project:
  default:
    return true;
  }
}

const char *ToScopeTitle(AssetBrowserScope scope) {
  switch (scope) {
  case AssetBrowserScope::CurrentScene:
    return "Current Scene";
  case AssetBrowserScope::UnusedInScene:
    return "Unused In Scene";
  case AssetBrowserScope::Broken:
    return "Broken Assets";
  case AssetBrowserScope::Textures:
    return "Textures";
  case AssetBrowserScope::Models:
    return "Models";
  case AssetBrowserScope::Scenes:
    return "Scenes";
  case AssetBrowserScope::Materials:
    return "Materials";
  case AssetBrowserScope::Skies:
    return "Skies";
  case AssetBrowserScope::Vfx:
    return "VFX";
  case AssetBrowserScope::Sequences:
    return "Sequences";
  case AssetBrowserScope::Project:
  default:
    return "Project Assets";
  }
}

const char *ToUsageBadge(const AssetRecord &record,
                         const AssetUsageSummary *usageSummary) {
  if (!usageSummary) {
    return "";
  }
  return usageSummary->IsUsed(record.guid) ? "Used" : "Unused";
}

const char *ToCookedBadge(const AssetRecord &record) {
  if (record.type == AssetType::Texture) {
    if (ASSETS::SEMANTICS::HasAssetArtifact(
            record, ASSETS::SEMANTICS::AssetArtifactKind::MainTexture)) {
      return "HTEX Ready";
    }
    if (ASSETS::SEMANTICS::HasAssetArtifact(
            record, ASSETS::SEMANTICS::AssetArtifactKind::DebugTextureDds)) {
      return "DDS Only";
    }
  }
  if (record.type == AssetType::Model) {
    const bool hasHmodel = ASSETS::SEMANTICS::HasAssetArtifact(
        record, ASSETS::SEMANTICS::AssetArtifactKind::MainModel);
    const bool hasHcmesh = ASSETS::SEMANTICS::HasAssetArtifact(
        record, ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry);
    const bool hasCollision = ASSETS::SEMANTICS::HasAssetArtifact(
        record, ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry);
    if (hasHmodel && hasHcmesh && hasCollision) {
      return "HMODEL + HCMESH + HCOLLISION";
    }
    if (hasHmodel && hasCollision) {
      return "HMODEL + HCOLLISION";
    }
    if (hasHmodel && hasHcmesh) {
      return "HMODEL + HCMESH";
    }
    if (hasHmodel) {
      return "HMODEL";
    }
    if (hasHcmesh) {
      return "HCMESH";
    }
    return "Raw Model";
  }
  if (record.type == AssetType::Scene) {
    return "Scene JSON";
  }
  return "";
}

const char *ToCompactTypeBadge(AssetType type) {
  switch (type) {
  case AssetType::Texture:
    return "TEX";
  case AssetType::Model:
    return "MDL";
  case AssetType::Scene:
    return "SCN";
  case AssetType::Material:
    return "MAT";
  case AssetType::Sky:
    return "SKY";
  case AssetType::VfxEffect:
    return "VFX";
  case AssetType::Animation:
    return "ANI";
  case AssetType::Particle:
    return "PTC";
  case AssetType::Sequence:
    return "SEQ";
  case AssetType::AnimationStateMachine:
    return "ASM";
  case AssetType::Unknown:
  default:
    return "UNK";
  }
}

const char *ToCompactStateBadge(AssetImportState state) {
  switch (state) {
  case AssetImportState::Imported:
    return "OK";
  case AssetImportState::Outdated:
    return "OUT";
  case AssetImportState::MetaOnly:
    return "META";
  case AssetImportState::MissingSource:
    return "MISS";
  case AssetImportState::MissingMeta:
    return "META?";
  case AssetImportState::MissingArtifact:
    return "ART?";
  case AssetImportState::UnknownImporter:
    return "IMP?";
  case AssetImportState::DuplicateGuid:
    return "DUP";
  case AssetImportState::ImportFailed:
    return "ERR";
  case AssetImportState::Unknown:
  default:
    return "UNK";
  }
}

const char *ToCompactUsageBadge(const AssetRecord &record,
                                const AssetUsageSummary *usageSummary) {
  if (!usageSummary) {
    return "";
  }
  return usageSummary->IsUsed(record.guid) ? "USE" : "IDLE";
}

const char *ToCompactCookedBadge(const AssetRecord &record) {
  if (record.type == AssetType::Texture) {
    if (ASSETS::SEMANTICS::HasAssetArtifact(
            record, ASSETS::SEMANTICS::AssetArtifactKind::MainTexture)) {
      return "HTEX";
    }
    if (ASSETS::SEMANTICS::HasAssetArtifact(
            record, ASSETS::SEMANTICS::AssetArtifactKind::DebugTextureDds)) {
      return "DDS";
    }
    return "RAW";
  }
  if (record.type == AssetType::Model) {
    const bool hasHmodel = ASSETS::SEMANTICS::HasAssetArtifact(
        record, ASSETS::SEMANTICS::AssetArtifactKind::MainModel);
    const bool hasHcmesh = ASSETS::SEMANTICS::HasAssetArtifact(
        record, ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry);
    const bool hasCollision = ASSETS::SEMANTICS::HasAssetArtifact(
        record, ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry);
    if (hasHmodel && hasHcmesh && hasCollision) {
      return "H+HC+C";
    }
    if (hasHmodel && hasCollision) {
      return "H+C";
    }
    if (hasHmodel && hasHcmesh) {
      return "H+HC";
    }
    if (hasHmodel) {
      return "HMDL";
    }
    if (hasHcmesh) {
      return "HC";
    }
    return "RAW";
  }
  if (record.type == AssetType::Scene) {
    return "JSON";
  }
  return "";
}

} // namespace HIKARI::EDITOR::ASSET_BROWSER
