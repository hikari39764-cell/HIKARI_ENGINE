#include "Render3D/Material/Program/HIKARI_MaterialProgramRuntimeDataBuilder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Material/Compiler/HIKARI_MaterialRampRasterizer.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::MATERIAL {

    namespace {
        std::string NormalizeId(std::string_view id) {
            std::string normalized{};
            normalized.reserve(id.size());
            for (const unsigned char c : id) {
                if (std::isalnum(c) != 0) {
                    normalized.push_back(
                        static_cast<char>(std::tolower(c)));
                }
            }
            return normalized;
        }

        void SetRgb(MATH::Vec4& target, const MATH::Vec4& value) {
            target.x = value.x;
            target.y = value.y;
            target.z = value.z;
        }

        std::vector<MATH::Vec4> BuildDefaultParameters() {
            std::vector<MATH::Vec4> values(
                kGpuMaterialProgramParameterSlotCount,
                MATH::Vec4{ 0.0f, 0.0f, 0.0f, 0.0f });
            values[0] = { 0.50f, 0.08f, 0.72f, 0.50f };
            values[1] = { 0.56f, 0.62f, 0.78f, 1.0f };
            values[2] = { 0.82f, 0.86f, 0.94f, 1.0f };
            values[3] = { 1.0f, 1.0f, 1.0f, 1.0f };
            values[4] = { 0.10f, 0.025f, 1.0f, 64.0f };
            values[5] = { 1.0f, 1.0f, 1.0f, 1.0f };
            values[6] = { 0.55f, 0.68f, 1.0f, 1.0f };
            values[7] = { 4.0f, 0.22f, 1.0f, 0.0f };
            values[8] = { 1.0f, 1.0f, 1.0f, 1.0f };
            values[9] = { 0.08f, 0.18f, -0.10f, 1.0f };
            values[10] = { 0.04f, 0.0f, 0.42f, 3.0f };
            values[11] = { 0.12f, 0.35f, 64.0f, 1.0f };
            values[12] = { 0.55f, 0.55f, 1.0f, 0.25f };
            values[13] = { 0.28f, 0.72f, 4.0f, 1.0f };
            values[14] = { 0.18f, 0.0f, 0.0f, 0.0f };
            values[15] = { 1.0f, 0.42f, 0.32f, 1.0f };
            values[16] = { 0.5f, 0.0f, 0.5f, 1.0f };
            values[17] = { 1.0f, 0.0f, 0.0f, 0.0f };
            values[18] = { 0.0f, 0.0f, 1.0f, 0.0f };
            values[19] = { 0.025f, 0.030f, 0.045f, 1.25f };
            return values;
        }

        void ApplyParameter(
            std::vector<MATH::Vec4>& values,
            const MaterialProgramParameterBinding& parameter) {
            const std::string id = NormalizeId(parameter.parameterId);
            MATH::Vec4 value = parameter.defaultValue.numeric;
            if (parameter.type == MaterialValueType::Bool) {
                value = {
                    parameter.defaultValue.boolean ? 1.0f : 0.0f,
                    0.0f,
                    0.0f,
                    0.0f
                };
            }
            if (id == "parametershadowthreshold") values[0].x = value.x;
            else if (id == "parametershadowsoftness") values[0].y = value.x;
            else if (id == "parametermidthreshold") values[0].z = value.x;
            else if (id == "parameterramprow") values[16].x = value.x;
            else if (id == "parameterrampmaskinfluence") values[16].y = value.x;
            else if (id == "parameterrampblend") values[16].z = value.x;
            else if (id == "parameterrampintensity") values[16].w = value.x;
            else if (id == "parametershadowtint") SetRgb(values[1], value);
            else if (id == "parametermidtint") SetRgb(values[2], value);
            else if (id == "parameterlittint") SetRgb(values[3], value);
            else if (id == "parameterspecularbandwidth") values[4].x = value.x;
            else if (id == "parameterspecularbandsoftness") values[4].y = value.x;
            else if (id == "parameterspecularintensity") values[4].z = value.x;
            else if (id == "parameterspecularpower") values[4].w = value.x;
            else if (id == "parameterspeculartint") SetRgb(values[5], value);
            else if (id == "parameterrimcolor") SetRgb(values[6], value);
            else if (id == "parameterrimintensity") values[6].w = value.x;
            else if (id == "parameterrimpower") values[7].x = value.x;
            else if (id == "parameterrimwidth") values[7].y = value.x;
            else if (id == "parameterrimlightalignment") values[7].z = value.x;
            else if (id == "parametermatcaptint") SetRgb(values[8], value);
            else if (id == "parametermatcapintensity") values[8].w = value.x;
            else if (id == "parameterhairshift") values[9].x = value.x;
            else if (id == "parameterhairwidth") values[9].y = value.x;
            else if (id == "parameterhairsecondaryshift") values[9].z = value.x;
            else if (id == "parameterhairintensity") values[9].w = value.x;
            else if (id == "parameterfaceshadowsoftness") values[10].x = value.x;
            else if (id == "parameterfaceshadowoffset") values[10].y = value.x;
            else if (id == "parameterfacehairshadowstrength") values[10].z = value.x;
            else if (id == "parameterfacehairshadowoffset") values[10].w = value.x;
            else if (id == "parameterfaceright") SetRgb(values[17], value);
            else if (id == "parameterfaceforward") SetRgb(values[18], value);
            else if (id == "parameteririsdepth") values[11].x = value.x;
            else if (id == "parametereyesparkle") values[11].y = value.x;
            else if (id == "parametereyesparklepower") values[11].z = value.x;
            else if (id == "parameterpbrnprblend") {
                values[12].x = value.x;
                values[12].y = value.x;
                values[12].z = value.x;
            }
            else if (id == "parameterdirectdiffusepbrblend") values[12].x = value.x;
            else if (id == "parameterdirectspecularpbrblend") values[12].y = value.x;
            else if (id == "parameterindirectpbrblend") values[12].z = value.x;
            else if (id == "parameterfabricsheen") values[13].x = value.x;
            else if (id == "parameterfiberroughness") values[13].y = value.x;
            else if (id == "parameterfabricsheenpower") values[13].z = value.x;
            else if (id == "parameterfabricsheenintensity") values[13].w = value.x;
            else if (id == "parameterskinwrap") values[14].x = value.x;
            else if (id == "parameterskinsubsurface") values[14].y = value.x;
            else if (id == "parameterskintint") SetRgb(values[15], value);
            else if (id == "parameteroutlinecolor") SetRgb(values[19], value);
            else if (id == "parameteroutlinethickness") values[19].w = value.x;
        }

        TextureResourceColorSpace ResolveColorSpace(
            MaterialResourceSemantic semantic) {
            return semantic == MaterialResourceSemantic::Ramp ||
                    semantic == MaterialResourceSemantic::MatCap
                ? TextureResourceColorSpace::Srgb
                : TextureResourceColorSpace::Linear;
        }

        bool IsRampResource(
            const MaterialProgramResourceBinding& binding) noexcept {
            return binding.semantic == MaterialResourceSemantic::Ramp ||
                binding.type == MaterialResourceType::Ramp1D ||
                binding.type == MaterialResourceType::Ramp2D;
        }

        std::string ToHex(uint64_t value) {
            std::ostringstream stream{};
            stream << std::hex << std::setfill('0') << std::setw(16) << value;
            return stream.str();
        }

        RuntimeTextureSlot BuildRampResourceSlot(
            const MaterialProgramResourceBinding& binding,
            const AssetRegistry& assetRegistry,
            std::string_view debugName) {
            RuntimeTextureSlot slot{};
            slot.texCoord = static_cast<int>(binding.texCoord);
            slot.uvScale = binding.uvScale;
            slot.uvOffset = binding.uvOffset;
            slot.uvRotation = binding.uvRotation;
            const auto* rampAsset =
                assetRegistry.FindAs<MaterialAssetDescriptor>(
                    AssetId{ binding.assetGuid.value });
            if (rampAsset == nullptr ||
                rampAsset->sourceData.kind != MaterialAssetKind::Ramp) {
                HIKARI_LOG_WARN(
                    "[MaterialProgram] neutral fallback for unresolved Ramp asset: " +
                    binding.assetGuid.value);
                return slot;
            }

            ASSETS::MATERIAL::MaterialRampRasterImage image{};
            std::string error{};
            if (!ASSETS::MATERIAL::RasterizeMaterialRamp(
                    rampAsset->sourceData.ramp,
                    image,
                    error)) {
                HIKARI_LOG_WARN(
                    "[MaterialProgram] neutral fallback for invalid Ramp asset " +
                    binding.assetGuid.value + ": " + error);
                return slot;
            }
            const std::string name =
                "material_ramp/" + std::string{ debugName } + "/" +
                binding.assetGuid.value + "/" +
                ToHex(image.contentHash);
            slot.sourcePath = rampAsset->sourcePath;
            slot.resolvedPath = "generated://" + name;
            // Ramp colors are stored as linear material values. Keeping the
            // generated SRV linear avoids applying an unintended second gamma
            // transform; imported image ramps still follow their SRGB policy.
            slot.resource = CreateRgbaTextureResource(
                name,
                image.width,
                image.height,
                image.rgba8,
                TextureResourceColorSpace::Linear);
            slot.handle = GetTextureResourceBackendHandle(slot.resource);
            slot.enabled = slot.handle >= 0;
            if (!slot.enabled) {
                HIKARI_LOG_WARN(
                    "[MaterialProgram] failed to create GPU Ramp texture: " +
                    binding.assetGuid.value);
            }
            return slot;
        }

        RuntimeTextureSlot BuildResourceSlot(
            const MaterialProgramResourceBinding& binding,
            const AssetRegistry& assetRegistry,
            std::string_view debugName) {
            RuntimeTextureSlot slot{};
            slot.texCoord = static_cast<int>(binding.texCoord);
            slot.uvScale = binding.uvScale;
            slot.uvOffset = binding.uvOffset;
            slot.uvRotation = binding.uvRotation;
            if (!binding.assetGuid.IsValid()) {
                return slot;
            }
            if (IsRampResource(binding)) {
                return BuildRampResourceSlot(
                    binding,
                    assetRegistry,
                    debugName);
            }
            const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(
                AssetId{ binding.assetGuid.value });
            if (texture == nullptr || texture->sourcePath.empty()) {
                HIKARI_LOG_WARN(
                    "[MaterialProgram] neutral fallback for unresolved resource: " +
                    binding.assetGuid.value);
                return slot;
            }
            slot.sourcePath = texture->sourcePath;
            slot.resolvedPath = texture->sourcePath;
            const std::string name =
                "material_program/" + std::string{ debugName } + "/" +
                binding.resourceId;
            slot.resource = binding.type == MaterialResourceType::TextureCube
                ? LoadCubemapResource(
                    name,
                    slot.resolvedPath,
                    ResolveColorSpace(binding.semantic))
                : LoadTextureResourceWithColorSpace(
                    name,
                    slot.resolvedPath,
                    ResolveColorSpace(binding.semantic));
            slot.handle = GetTextureResourceBackendHandle(slot.resource);
            slot.enabled = slot.handle >= 0;
            if (!slot.enabled) {
                HIKARI_LOG_WARN(
                    "[MaterialProgram] neutral fallback for failed resource: " +
                    slot.resolvedPath);
            }
            return slot;
        }

        uint32_t ResolveResourceSlot(
            const MaterialProgramResourceBinding& resource,
            uint32_t rampIndex) {
            if (resource.semantic == MaterialResourceSemantic::Ramp) {
                return (std::min)(rampIndex, 1u);
            }
            if (resource.semantic == MaterialResourceSemantic::MatCap) return 2u;
            if (resource.semantic == MaterialResourceSemantic::FaceSdf) return 3u;
            const std::string id = NormalizeId(resource.resourceId);
            if (id.find("hair") != std::string::npos ||
                id.find("strand") != std::string::npos) return 5u;
            if (id.find("eye") != std::string::npos ||
                id.find("iris") != std::string::npos) return 6u;
            if (resource.semantic == MaterialResourceSemantic::Mask) return 4u;
            return 7u;
        }

        uint32_t BuildFlags(
            const MaterialProgramIR& program,
            MaterialProgramResolutionKind resolutionKind) {
            using namespace GPU_MATERIAL_PROGRAM_FLAGS;
            uint32_t flags = 0u;
            if (program.surface.unlit) flags |= Unlit;
            if (program.surface.alphaMode == MaterialAlphaMode::Mask) flags |= AlphaMask;
            if (program.surface.alphaMode == MaterialAlphaMode::Blend) flags |= AlphaBlend;
            if (program.surface.doubleSided) flags |= DoubleSided;
            if (resolutionKind == MaterialProgramResolutionKind::LastGood) flags |= UsedLastGood;
            if (resolutionKind == MaterialProgramResolutionKind::StandardPbrFallback) flags |= UsedFallback;
            const auto has = [&](MaterialProgramCapability capability) {
                return (program.capabilities & MaterialCapabilityBit(capability)) != 0u;
            };
            if (has(MaterialProgramCapability::StandardPbr)) flags |= StandardPbr;
            if (has(MaterialProgramCapability::RampDiffuse)) flags |= RampDiffuse;
            if (has(MaterialProgramCapability::MultiRampDiffuse)) flags |= MultiRampDiffuse;
            if (has(MaterialProgramCapability::SpecularBand)) flags |= SpecularBand;
            if (has(MaterialProgramCapability::FresnelRim)) flags |= FresnelRim;
            if (has(MaterialProgramCapability::MatCap)) flags |= MatCap;
            if (has(MaterialProgramCapability::AnisotropicHair)) flags |= AnisotropicHair;
            if (has(MaterialProgramCapability::FaceSdf)) flags |= FaceSdf;
            if (has(MaterialProgramCapability::EyeLighting)) flags |= EyeLighting;
            switch (program.templateId) {
            case MaterialTemplateId::GenericToon: flags |= TemplateGenericToon; break;
            case MaterialTemplateId::Skin: flags |= TemplateSkin; break;
            case MaterialTemplateId::Face: flags |= TemplateFace; break;
            case MaterialTemplateId::Hair: flags |= TemplateHair; break;
            case MaterialTemplateId::Eye: flags |= TemplateEye; break;
            case MaterialTemplateId::Cloth: flags |= TemplateCloth; break;
            case MaterialTemplateId::HybridMetal: flags |= TemplateHybrid; break;
            default: break;
            }
            for (MaterialFeatureRequest request : program.featureRequests) {
                switch (request) {
                case MaterialFeatureRequest::GeometryOutline:
                    flags |= RenderFeatureGeometryOutline;
                    break;
                case MaterialFeatureRequest::ScreenOutline:
                    flags |= RenderFeatureScreenOutline;
                    break;
                case MaterialFeatureRequest::VisibilityOverlay:
                    flags |= RenderFeatureVisibilityOverlay;
                    break;
                case MaterialFeatureRequest::FaceHairShadow:
                    flags |= RenderFeatureFaceHairShadow;
                    break;
                }
            }
            return flags;
        }

        bool RequiresMaterialProgramBlock(
            const MaterialProgramIR& program) noexcept {

            constexpr uint64_t kRuntimeResponseCapabilities =
                MaterialCapabilityBit(MaterialProgramCapability::RampDiffuse) |
                MaterialCapabilityBit(MaterialProgramCapability::MultiRampDiffuse) |
                MaterialCapabilityBit(MaterialProgramCapability::SpecularBand) |
                MaterialCapabilityBit(MaterialProgramCapability::FresnelRim) |
                MaterialCapabilityBit(MaterialProgramCapability::MatCap) |
                MaterialCapabilityBit(MaterialProgramCapability::AnisotropicHair) |
                MaterialCapabilityBit(MaterialProgramCapability::FaceSdf) |
                MaterialCapabilityBit(MaterialProgramCapability::EyeLighting);
            return program.templateId != MaterialTemplateId::StandardPbr ||
                (program.capabilities & kRuntimeResponseCapabilities) != 0u ||
                !program.featureRequests.empty();
        }
    }

    MaterialProgramRuntimeData BuildMaterialProgramRuntimeData(
        const MaterialProgramIR& program,
        MaterialProgramResolutionKind resolutionKind,
        const AssetRegistry& assetRegistry,
        std::string_view debugName) {
        MaterialProgramRuntimeData data{};
        data.instanceFlags = BuildFlags(program, resolutionKind);
        if (!RequiresMaterialProgramBlock(program)) {
            return data;
        }
        data.parameters = BuildDefaultParameters();
        for (const MaterialProgramParameterBinding& parameter : program.parameters) {
            ApplyParameter(data.parameters, parameter);
        }
        data.resources.resize(kGpuMaterialProgramResourceSlotCount);
        uint32_t rampIndex = 0u;
        for (const MaterialProgramResourceBinding& resource : program.resources) {
            const uint32_t slot = ResolveResourceSlot(resource, rampIndex);
            if (resource.semantic == MaterialResourceSemantic::Ramp) {
                ++rampIndex;
            }
            data.resources[slot] = BuildResourceSlot(
                resource,
                assetRegistry,
                debugName);
        }
        return data;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
