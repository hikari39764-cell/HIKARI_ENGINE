#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"

#include <string_view>
#include <type_traits>

#include "RenderContracts/Material/HIKARI_MaterialGraphQuery.h"

namespace HIKARI {

void SyncPbrFromGraphParameters(MaterialAssetData& material) {
    const auto apply = [&](std::string_view id, auto& target) {
        const auto* parameter = FindMaterialGraphParameter(material.graph, id);
        if (!parameter || !IsMaterialNumericType(parameter->defaultValue.type))
            return;
        const auto& value = parameter->defaultValue;
        using Target = std::remove_reference_t<decltype(target)>;
        if constexpr (std::is_same_v<Target, float>) {
            target = value.numeric.x;
        } else {
            target.x = value.numeric.x;
            target.y = value.numeric.y;
            target.z = value.numeric.z;
            if constexpr (std::is_same_v<Target, MATH::Vec4>) {
                if (value.type == MaterialValueType::Float4)
                    target.w = value.numeric.w;
            }
        }
    };
    apply("parameter.baseColor", material.pbr.baseColorFactor);
    apply("parameter.metallic", material.pbr.metallicFactor);
    apply("parameter.roughness", material.pbr.roughnessFactor);
    apply("parameter.specular", material.pbr.specularFactor);
    apply("parameter.specularColor", material.pbr.specularColorFactor);
    apply("parameter.normalScale", material.pbr.normalScale);
    apply("parameter.occlusionStrength", material.pbr.occlusionStrength);
    apply("parameter.emissive", material.pbr.emissiveFactor);
    apply("parameter.emissiveStrength", material.pbr.emissiveStrength);
}

} // namespace HIKARI
