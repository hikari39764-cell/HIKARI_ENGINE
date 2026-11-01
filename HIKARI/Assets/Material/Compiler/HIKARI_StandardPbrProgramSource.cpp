#include "Assets/Material/Compiler/HIKARI_StandardPbrProgramSource.h"

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI::ASSETS::MATERIAL {

RENDER3D::MATERIAL::StandardPbrProgramSource MakeStandardPbrProgramSource() {
    return {BuildStandardPbrMaterialProgramFallback(),
            [](const MaterialAssetData& data) { return ::HIKARI::CompileMaterialAsset(data); }};
}

} // namespace HIKARI::ASSETS::MATERIAL
