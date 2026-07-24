#pragma once

#include <string>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI {

    class AssetDatabase;
    struct EditorSelection;

    namespace EDITOR {

        bool DrawMaterialTextureSlot(
            const char* label,
            MaterialTextureSlotData& slot,
            AssetDatabase& assetDatabase,
            AssetRegistry& assetRegistry,
            EditorSelection* selection);

        void ClearMaterialTextureSlotPreviewCache();
        void InvalidateMaterialTextureSlotPreviewByGuid(const AssetGuid& guid);
        void InvalidateMaterialTextureSlotPreviewByPath(const std::string& path);

    } // namespace EDITOR

} // namespace HIKARI
