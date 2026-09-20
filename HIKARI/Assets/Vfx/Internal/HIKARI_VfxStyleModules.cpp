#include "Assets/Vfx/Internal/HIKARI_VfxCompositionBuilder.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"

namespace HIKARI::VFX::PRESETS::INTERNAL {
    VfxAssetData StyleModule(std::string_view id) {
        VfxAssetData asset;
        asset.name=FindVfxPreset(id)->displayName;
        auto layer=Shape(std::string(id),VfxRenderShapeType::Sprite,0,0);
        layer.name=asset.name;
        layer.kind=VfxLayerKind::Adjustment;
        const auto add=[&](MaterialEffectKind kind)->MaterialEffectStep& {
            layer.effects.push_back(MakeMaterialEffect(kind,layer.id+"."+std::string(MaterialEffectName(kind))));
            return layer.effects.back();
        };
        if (id=="style_pixel") {
            add(MaterialEffectKind::Pixelate).parameters={0.016f,1,0,0};
            add(MaterialEffectKind::Dither).parameters={6,0.004f,0.35f,1};
        } else if (id=="style_ink") {
            auto& ink=add(MaterialEffectKind::Ink);
            ink.parameters={0.005f,1,0.2f,0.9f};ink.parameters2={0.004f,1,0.15f,0.2f};
            ink.colorA={0.025f,0.04f,0.055f,1};
        } else if (id=="style_comic") {
            add(MaterialEffectKind::Posterize).parameters={5,1,0,0};
            add(MaterialEffectKind::Halftone).mix=0.35f;
            auto& outline=add(MaterialEffectKind::Outline);outline.colorA={0.02f,0.025f,0.04f,1};
        } else if (id=="style_hologram") {
            auto& grade=add(MaterialEffectKind::Duotone);
            grade.colorA={0.02f,0.08f,0.3f,1};grade.colorB={0.1f,1.2f,1.5f,1};
            add(MaterialEffectKind::Scanlines).parameters={0.012f,0.3f,0,0};
            add(MaterialEffectKind::Chromatic).parameters={0.004f,0,0,0.5f};
        } else if (id=="style_heat") {
            auto& distortion=add(MaterialEffectKind::TurbulentDisplace);
            distortion.space=MaterialEffectSpace::Group3D;
            distortion.parameters={0.03f,9,0.25f,0.35f};
            add(MaterialEffectKind::Chromatic).parameters={0.003f,90,0,0.3f};
        } else if (id=="style_glow") {
            add(MaterialEffectKind::Glow).parameters={1,0.5f,0.15f,0.03f};
            auto& streak=add(MaterialEffectKind::Streak);streak.parameters={0.0625f,0,4,0.4f};streak.mix=0.15f;
        }
        asset.emitters.push_back(std::move(layer));
        return asset;
    }
}
