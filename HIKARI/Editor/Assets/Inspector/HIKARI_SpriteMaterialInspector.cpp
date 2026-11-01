#include "Editor/Assets/Inspector/HIKARI_AssetInspectorPanel.h"

#include <algorithm>

#include "Assets/HIKARI_AssetRegistry.h"
#include "RenderContracts/Shader/HIKARI_ProjectSpriteShaderContract.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"
#include "Editor/Assets/Inspector/HIKARI_AssetInspectorInternal.h"
#include "Editor/Widgets/HIKARI_TypedValueEditor.h"

namespace HIKARI {
bool AssetInspectorPanel::ResolveSpriteInspectorShader(AssetRegistry& registry, const AssetGuid& guid) const {
    const auto* descriptor = registry.FindAs<ShaderAssetDescriptor>(AssetId{guid.value});
    const auto program = descriptor ? descriptor->program : nullptr;
    if (!program) {
        spriteShader_.reset();
        spriteShaderValid_ = false;
        spriteShaderDiagnostic_ = TEXT::Localize("ui.spriteMaterial.shaderUnavailable");
    } else if (spriteShader_ != program) {
        spriteShader_ = program;
        spriteShaderValid_ = ASSETS::SHADER::ValidateProjectSpriteShaderArtifact(*program, spriteShaderDiagnostic_);
    }
    return spriteShaderValid_;
}

bool AssetInspectorPanel::DrawSpriteShaderCreation(AssetDatabase& database, AssetRegistry& registry,
                                                   const AssetRecord& record, EditorSelection& selection) const {
    bool invalidated = false;
    const bool valid = ResolveSpriteInspectorShader(registry, record.guid);
    ImGui::TextWrapped("%s", TEXT::Localize("ui.spriteMaterial.createHint").c_str());
    if (!spriteShaderDiagnostic_.empty())
        ImGui::TextWrapped("%s", spriteShaderDiagnostic_.c_str());
    ImGui::BeginDisabled(!valid || database.GetQueuedImportStatus().active);
    if (ImGui::Button(TEXT::Localize("ui.spriteMaterial.create").c_str())) {
        const auto directory = database.GetProjectRoot() / "Assets/Materials";
        const std::string name = EDITOR::ASSET_BROWSER::SanitizeFileToken(record.displayName, "Sprite Material");
        const auto path = EDITOR::ASSET_BROWSER::MakeUniqueCompoundSuffixFilePath(directory, name, ".hspritematerial");
        SPRITE::SpriteMaterialAssetData material;
        material.name = TEXT::PathToUtf8(path.stem());
        material.shaderAssetGuid = record.guid;
        std::string error;
        const bool saved = SPRITE::WriteSpriteMaterialAsset(path, material, error);
        invalidated = saved;
        if (!saved || !database.ScanAssets(true)) {
            spriteMaterialStatus_ =
                error.empty() ? TEXT::Localize("ui.assetBrowser.assetDatabaseRefreshFailed") : error;
        } else if (const auto* created = database.FindByPath(path)) {
            // ScanAssets は record を無効化するため、以後は新しい GUID のコピーだけを使う。
            const AssetGuid createdGuid = created->guid;
            EDITOR::ASSET_BROWSER::SelectRecord(*created, selection);
            if (!database.QueueImportAssets({createdGuid}, "Created Sprite material"))
                spriteMaterialStatus_ = TEXT::Localize("ui.assetInspector.materialSavedWaitImport");
            else {
                pendingRefreshRuntimeMaterialGuid_ = createdGuid.value;
                spriteMaterialStatus_.clear();
            }
        }
    }
    ImGui::EndDisabled();
    if (!spriteMaterialStatus_.empty())
        ImGui::TextWrapped("%s", spriteMaterialStatus_.c_str());
    return invalidated;
}

void AssetInspectorPanel::DrawSpriteMaterialEditor(AssetDatabase& database, AssetRegistry& registry,
                                                   const AssetRecord& record) const {
    if (spriteMaterialProject_ != database.GetProjectRoot()) {
        spriteMaterialProject_ = database.GetProjectRoot();
        spriteMaterialCache_.Configure(&database);
        spriteMaterialGuid_.clear();
    }
    if (spriteMaterialGuid_ != record.guid.value ||
        (!spriteMaterialDirty_ && spriteMaterialRevision_ != database.GetContentRevision())) {
        spriteMaterialGuid_ = record.guid.value;
        spriteMaterialRevision_ = database.GetContentRevision();
        const auto asset = spriteMaterialCache_.Load(record.guid, {}, spriteMaterialStatus_, true);
        spriteMaterialLoaded_ = static_cast<bool>(asset);
        spriteMaterialEdit_ = asset ? *asset : SPRITE::SpriteMaterialAssetData{};
        spriteMaterialDirty_ = false;
    }
    if (!spriteMaterialLoaded_) {
        ImGui::TextWrapped("%s", spriteMaterialStatus_.c_str());
        return;
    }

    auto& data = spriteMaterialEdit_;
    char name[128]{};
    EDITOR::ASSET_INSPECTOR::CopyToBuffer(data.name, name, sizeof(name));
    if (ImGui::InputText(TEXT::Localize("ui.assetInspector.name").c_str(), name, sizeof(name))) {
        data.name = name;
        spriteMaterialDirty_ = true;
    }
    ImGui::Text("Shader: %s", data.shaderAssetGuid.value.c_str());
    ImGui::TextWrapped("%s", TEXT::Localize("ui.spriteMaterial.editHint").c_str());
    const bool valid = ResolveSpriteInspectorShader(registry, data.shaderAssetGuid);
    if (!spriteShaderDiagnostic_.empty())
        ImGui::TextWrapped("%s", spriteShaderDiagnostic_.c_str());
    if (valid) {
        std::string group;
        for (const auto& definition : spriteShader_->parameters) {
            if (group != definition.group) {
                group = definition.group;
                if (!group.empty())
                    ImGui::SeparatorText(group.c_str());
            }
            auto overrideValue = std::find_if(data.parameters.begin(), data.parameters.end(),
                                              [&](const auto& value) { return value.key == definition.key; });
            BINDING::Value value =
                overrideValue == data.parameters.end() ? definition.defaultValue : overrideValue->value;
            const EDITOR::TypedValueRange range{definition.hasRange, definition.minimum, definition.maximum,
                                                definition.step > 0.0f ? definition.step : 0.01f};
            ImGui::PushID(definition.key.c_str());
            if (EDITOR::DrawTypedEditorValue(definition.label.c_str(), value, range, -120.0f).changed) {
                BINDING::Value canonical;
                if (ASSETS::SHADER::ValidateShaderParameterValue(definition, value, canonical, spriteMaterialStatus_)) {
                    if (overrideValue == data.parameters.end())
                        data.parameters.push_back({definition.key, canonical});
                    else
                        overrideValue->value = canonical;
                    spriteMaterialDirty_ = true;
                }
            }
            // 編集により vector が再確保される場合があるため iterator を再利用しない。
            const auto current = std::find_if(data.parameters.begin(), data.parameters.end(),
                                              [&](const auto& value) { return value.key == definition.key; });
            if (current != data.parameters.end()) {
                ImGui::SameLine();
                if (ImGui::SmallButton(TEXT::Localize("ui.spriteMaterial.reset").c_str())) {
                    data.parameters.erase(current);
                    spriteMaterialDirty_ = true;
                }
            }
            ImGui::PopID();
        }
    }
    if (!data.parameters.empty() && ImGui::Button(TEXT::Localize("ui.spriteMaterial.resetAll").c_str())) {
        data.parameters.clear();
        spriteMaterialDirty_ = true;
    }
    const auto path = database.ResolveSourcePath(record);
    ImGui::BeginDisabled(!valid || !spriteMaterialDirty_ || database.GetQueuedImportStatus().active);
    if (ImGui::Button(TEXT::Localize("ui.assetInspector.saveMaterial").c_str())) {
        if (SPRITE::ValidateSpriteMaterialAsset(data, spriteShader_->parameters, spriteMaterialStatus_) &&
            SPRITE::WriteSpriteMaterialAsset(path, data, spriteMaterialStatus_)) {
            spriteMaterialDirty_ = false;
            spriteMaterialCache_.Configure(&database);
            const bool queued = database.QueueImportAssets({record.guid}, "Saved Sprite material");
            if (queued)
                pendingRefreshRuntimeMaterialGuid_ = record.guid.value;
            spriteMaterialStatus_ = TEXT::Localize(queued ? "ui.assetInspector.materialSaved"
                                                          : "ui.assetInspector.materialSavedWaitImport");
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.assetInspector.revert").c_str())) {
        spriteMaterialCache_.Configure(&database);
        spriteMaterialGuid_.clear();
        spriteMaterialDirty_ = false;
    }
    if (spriteMaterialDirty_)
        ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.unsavedEdits").c_str());
    if (!spriteMaterialStatus_.empty())
        ImGui::TextWrapped("%s", spriteMaterialStatus_.c_str());
}
} // namespace HIKARI
