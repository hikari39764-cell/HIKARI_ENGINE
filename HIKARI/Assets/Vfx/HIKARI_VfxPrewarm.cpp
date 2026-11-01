#include "Assets/Vfx/HIKARI_VfxPrewarm.h"

#include "Assets/Vfx/HIKARI_VfxProgram.h"

namespace HIKARI::VFX {

std::vector<VfxPrewarmMaterial> CollectVfxPrewarmMaterials(const VfxProgram& program) {
    std::vector<VfxPrewarmMaterial> result;
    for (const VfxProgramEmitter& emitter : program.emitters) {
        const auto& render = emitter.render;
        if ((emitter.kind != VfxLayerKind::Particles && emitter.kind != VfxLayerKind::Shape) ||
            !render.meshAssetGuid.empty()) {
            continue;
        }
        VfxPrewarmMaterial& material = result.emplace_back();
        material.materialMode = render.materialMode;
        material.colorTextureAssetGuid = render.colorTextureAssetGuid;
        material.materialAssetGuid = render.materialAssetGuid;
        material.effectMaterial = render.effectMaterial;
        material.effectMaterialKey = render.effectMaterialKey;
    }
    return result;
}

} // namespace HIKARI::VFX
