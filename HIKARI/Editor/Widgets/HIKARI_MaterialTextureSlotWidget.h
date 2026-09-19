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

    } // namespace EDITOR

} // namespace HIKARI
