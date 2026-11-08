#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"
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
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
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

#include "imgui.h"

#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"

namespace HIKARI::EDITOR::ASSET_BROWSER {

namespace {
// 先頭は「すべて」。並びが種類フィルターの選択肢の表示順になる。
constexpr AssetTypeFilter kAssetTypeFilters[] = {
    {AssetType::Unknown, "ui.assetBrowser.filterAll"},
    {AssetType::Texture, "ui.assetBrowser.filterTexture"},
    {AssetType::Model, "ui.assetBrowser.filterModel"},
    {AssetType::Scene, "ui.assetBrowser.filterScene"},
    {AssetType::Sky, "ui.assetBrowser.filterSky"},
    {AssetType::Material, "ui.assetBrowser.filterMaterial"},
    {AssetType::VfxEffect, "ui.assetBrowser.filterVfx"},
    {AssetType::Sequence, "ui.assetBrowser.filterSequence"},
    {AssetType::AnimationStateMachine, "ui.assetBrowser.filterAnimationStateMachine"},
    {AssetType::Dialogue, "ui.assetBrowser.filterDialogue"},
    {AssetType::ModelMaterialBinding, "ui.assetBrowser.filterMaterialSet"},
    {AssetType::Font, "textstyle.font"},
    {AssetType::TextStyle, "textstyle.asset"},
    {AssetType::PostEffect, "post.effect.asset"},
    {AssetType::PostProfile, "post.profile.asset"},
    {AssetType::Environment, "environment.profile.asset"},
    {AssetType::Weather, "environment.weather.asset"},
    {AssetType::RenderStyle, "renderstyle.asset"},
    {AssetType::ColorLut, "environment.colorLut.asset"},
    {AssetType::LightProfile, "light.profile.asset"},
    {AssetType::Audio, "cine.sound"},
};
} // namespace

std::span<const AssetTypeFilter> GetAssetTypeFilters() noexcept { return kAssetTypeFilters; }

AssetType TypeFromFilterIndex(int index) {
    return index > 0 && static_cast<size_t>(index) < std::size(kAssetTypeFilters) ? kAssetTypeFilters[index].type
                                                                                  : AssetType::Unknown;
}

bool MatchesTypeFilter(const AssetRecord& record, int typeFilter) {
    if (typeFilter == 0) {
        return true;
    }
    return record.type == TypeFromFilterIndex(typeFilter);
}

bool MatchesStateFilter(const AssetRecord& record, int stateFilter) {
    if (stateFilter == 0) {
        return true;
    }

    const AssetImportState state = GetImportState(record);
    switch (stateFilter) {
    case 1: return state == AssetImportState::Imported;
    case 2: return state == AssetImportState::Outdated;
    case 3:
        return state == AssetImportState::MissingSource || state == AssetImportState::MissingMeta ||
               state == AssetImportState::MissingArtifact;
    case 4:
        return state == AssetImportState::UnknownImporter || state == AssetImportState::DuplicateGuid ||
               state == AssetImportState::ImportFailed;
    case 5: return state == AssetImportState::MetaOnly;
    default: return true;
    }
}

bool MatchesSearch(const AssetRecord& record, const char* searchText) {
    if (!searchText || searchText[0] == '\0') {
        return true;
    }

    const std::string needle = TEXT::ToLowerAsciiCopy(searchText);
    const std::string haystack =
        TEXT::ToLowerAsciiCopy(record.displayName + " " + record.guid.value + " " + record.sourcePath.generic_string() +
                               " " + record.meta.importerId);
    return haystack.find(needle) != std::string::npos;
}

bool MatchesScope(const AssetRecord& record, const AssetUsageSummary* usageSummary, AssetBrowserScope scope) {
    switch (scope) {
    case AssetBrowserScope::CurrentScene: return usageSummary && usageSummary->IsUsed(record.guid);
    case AssetBrowserScope::UnusedInScene: return usageSummary && !usageSummary->IsUsed(record.guid);
    case AssetBrowserScope::Broken: return IsBrokenAssetRecord(record);
    case AssetBrowserScope::Textures: return record.type == AssetType::Texture;
    case AssetBrowserScope::Models: return record.type == AssetType::Model;
    case AssetBrowserScope::Scenes: return record.type == AssetType::Scene;
    case AssetBrowserScope::Materials:
        return record.type == AssetType::Material || record.type == AssetType::ModelMaterialBinding;
    case AssetBrowserScope::Skies: return record.type == AssetType::Sky;
    case AssetBrowserScope::Vfx: return record.type == AssetType::VfxEffect;
    case AssetBrowserScope::Sequences: return record.type == AssetType::Sequence;
    case AssetBrowserScope::Project:
    default: return true;
    }
}

std::string ToScopeTitle(AssetBrowserScope scope) {
    switch (scope) {
    case AssetBrowserScope::CurrentScene: return TEXT::Localize("ui.assetBrowser.scopeTitle.currentScene");
    case AssetBrowserScope::UnusedInScene: return TEXT::Localize("ui.assetBrowser.scopeTitle.unused");
    case AssetBrowserScope::Broken: return TEXT::Localize("ui.assetBrowser.scopeTitle.broken");
    case AssetBrowserScope::Textures: return TEXT::Localize("ui.assetBrowser.scopeTitle.textures");
    case AssetBrowserScope::Models: return TEXT::Localize("ui.assetBrowser.scopeTitle.models");
    case AssetBrowserScope::Scenes: return TEXT::Localize("ui.assetBrowser.scopeTitle.scenes");
    case AssetBrowserScope::Materials: return TEXT::Localize("ui.assetBrowser.scopeTitle.materials");
    case AssetBrowserScope::Skies: return TEXT::Localize("ui.assetBrowser.scopeTitle.skies");
    case AssetBrowserScope::Vfx: return TEXT::Localize("ui.assetBrowser.scopeTitle.vfx");
    case AssetBrowserScope::Sequences: return TEXT::Localize("ui.assetBrowser.scopeTitle.sequences");
    case AssetBrowserScope::Project:
    default: return TEXT::Localize("ui.assetBrowser.scopeTitle.project");
    }
}

std::string ToUsageBadge(const AssetRecord& record, const AssetUsageSummary* usageSummary) {
    if (!usageSummary) {
        return {};
    }
    return usageSummary->IsUsed(record.guid) ? TEXT::Localize("ui.assetBrowser.usageUsed")
                                             : TEXT::Localize("ui.assetBrowser.usageUnused");
}

std::string ToCookedBadge(const AssetRecord& record) {
    if (record.type == AssetType::Texture) {
        if (ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::MainTexture)) {
            return TEXT::Localize("ui.assetBrowser.cookedHtexReady");
        }
        if (ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::DebugTextureDds)) {
            return TEXT::Localize("ui.assetBrowser.cookedDdsOnly");
        }
    }
    if (record.type == AssetType::Model) {
        const bool hasHmodel =
            ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::MainModel);
        const bool hasHcmesh =
            ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry);
        const bool hasCollision =
            ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry);
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
        return TEXT::Localize("ui.assetBrowser.cookedRawModel");
    }
    if (record.type == AssetType::Scene) {
        return TEXT::Localize("ui.assetBrowser.cookedSceneJson");
    }
    return {};
}

const char* ToCompactTypeBadge(AssetType type) {
    switch (type) {
    case AssetType::Texture: return "TEX";
    case AssetType::Model: return "MDL";
    case AssetType::Scene: return "SCN";
    case AssetType::Material: return "MAT";
    case AssetType::ModelMaterialBinding: return "MSET";
    case AssetType::Sky: return "SKY";
    case AssetType::VfxEffect: return "VFX";
    case AssetType::Animation: return "ANI";
    case AssetType::Particle: return "PTC";
    case AssetType::Sequence: return "SEQ";
    case AssetType::AnimationStateMachine: return "ASM";
    case AssetType::Dialogue: return "DLG";
    case AssetType::Font: return "FONT";
    case AssetType::TextStyle: return "TXT";
    case AssetType::PostEffect: return "PFX";
    case AssetType::PostProfile: return "PPR";
    case AssetType::Environment: return "ENV";
    case AssetType::Weather: return "WTH";
    case AssetType::RenderStyle: return "RST";
    case AssetType::ColorLut: return "LUT";
    case AssetType::LightProfile: return "IES";
    case AssetType::Audio: return "SND";
    case AssetType::Unknown:
    default: return "UNK";
    }
}

const char* ToCompactStateBadge(AssetImportState state) {
    switch (state) {
    case AssetImportState::Imported: return "OK";
    case AssetImportState::Outdated: return "OUT";
    case AssetImportState::MetaOnly: return "META";
    case AssetImportState::MissingSource: return "MISS";
    case AssetImportState::MissingMeta: return "META?";
    case AssetImportState::MissingArtifact: return "ART?";
    case AssetImportState::UnknownImporter: return "IMP?";
    case AssetImportState::DuplicateGuid: return "DUP";
    case AssetImportState::ImportFailed: return "ERR";
    case AssetImportState::Unknown:
    default: return "UNK";
    }
}

const char* ToCompactUsageBadge(const AssetRecord& record, const AssetUsageSummary* usageSummary) {
    if (!usageSummary) {
        return "";
    }
    return usageSummary->IsUsed(record.guid) ? "USE" : "IDLE";
}

const char* ToCompactCookedBadge(const AssetRecord& record) {
    if (record.type == AssetType::Texture) {
        if (ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::MainTexture)) {
            return "HTEX";
        }
        if (ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::DebugTextureDds)) {
            return "DDS";
        }
        return "RAW";
    }
    if (record.type == AssetType::Model) {
        const bool hasHmodel =
            ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::MainModel);
        const bool hasHcmesh =
            ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry);
        const bool hasCollision =
            ASSETS::SEMANTICS::HasAssetArtifact(record, ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry);
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
