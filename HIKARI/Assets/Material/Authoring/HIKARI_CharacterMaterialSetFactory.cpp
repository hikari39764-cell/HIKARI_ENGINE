#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"

#include <array>
#include <string>
#include <string_view>

namespace HIKARI::ASSETS::MATERIAL {

    DefaultCharacterMaterialSet CreateDefaultCharacterMaterialSet(
        std::string_view displayNamePrefix) {

        const std::string prefix = displayNamePrefix.empty()
            ? "Character" : std::string(displayNamePrefix);
        struct EntrySeed {
            std::string_view roleId;
            std::string_view label;
            std::string_view use;
            MaterialTemplateId templateId;
        };
        constexpr std::array<EntrySeed, 6u> seeds{ {
            { "character.skin", "Skin",
                "skin and exposed body surfaces", MaterialTemplateId::Skin },
            { "character.face", "Face",
                "face surfaces that can provide an optional Face SDF",
                MaterialTemplateId::Face },
            { "character.hair", "Hair",
                "hair cards or strand-like surfaces", MaterialTemplateId::Hair },
            { "character.eye", "Eye",
                "iris, cornea and eye-highlight surfaces", MaterialTemplateId::Eye },
            { "character.cloth", "Cloth",
                "fabric, uniform and soft accessory surfaces",
                MaterialTemplateId::Cloth },
            { "character.hybridMetal", "Hybrid Metal",
                "metal accessories requiring both PBR and stylized response",
                MaterialTemplateId::HybridMetal },
        } };

        DefaultCharacterMaterialSet result{};
        result.entries.reserve(seeds.size());
        for (const EntrySeed& seed : seeds) {
            const std::string name = prefix + " " + std::string(seed.label);
            result.entries.push_back(CharacterMaterialSetEntry{
                std::string(seed.roleId),
                name,
                std::string(seed.use),
                seed.templateId,
                CreateMaterialFromTemplate(seed.templateId, name)
            });
        }
        result.diagnostics = {
            "Default Character Material Set is role-based; it does not infer or hard-code model slot names or indices.",
            "Unassigned Ramp resources use the engine neutral ramp fallback until an authored Ramp asset is bound.",
            "Face SDF, mask and MatCap resources are intentionally optional and use semantic fallbacks when unassigned."
        };
        return result;
    }

} // namespace HIKARI::ASSETS::MATERIAL
