#include "Render3D/Material/Program/HIKARI_MaterialProgramRuntimeDataBuilder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Material/Compiler/HIKARI_MaterialRampRasterizer.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramStaticEvaluator.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::MATERIAL {

    namespace {
        struct CachedRampResource {
            uint64_t contentHash = 0u;
            TextureResourceHandle resource{};
        };

        std::unordered_map<std::string, CachedRampResource>
            gRampResourcesByAsset{};

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

        std::vector<MATH::Vec4> BuildDefaultParameters() {
            std::vector<MATH::Vec4> values(
                kGpuMaterialProgramParameterSlotCount,
                MATH::Vec4{ 0.0f, 0.0f, 0.0f, 0.0f });
            values[0] = { 0.50f, 0.08f, 0.72f, 1.0f };
            values[1] = { 0.56f, 0.62f, 0.78f, 1.0f };
            values[2] = { 0.82f, 0.86f, 0.94f, 1.0f };
            values[3] = { 1.0f, 1.0f, 1.0f, 1.0f };
            values[4] = { 0.10f, 0.025f, 1.0f, 64.0f };
            values[5] = { 1.0f, 1.0f, 1.0f, 1.0f };
            values[6] = { 0.55f, 0.68f, 1.0f, 1.0f };
            values[7] = { 4.0f, 0.22f, 1.0f, 0.0f };
            values[8] = { 1.0f, 1.0f, 1.0f, 1.0f };
            values[9] = { 0.08f, 0.18f, -0.10f, 1.0f };
            values[10] = { 0.04f, 0.0f, 0.65f, 0.0f };
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

        TextureResourceColorSpace ResolveColorSpace(
            MaterialResourceSemantic semantic) {
            return semantic == MaterialResourceSemantic::BaseColor ||
                    semantic == MaterialResourceSemantic::Emissive ||
                    semantic == MaterialResourceSemantic::SpecularColor ||
                    semantic == MaterialResourceSemantic::Ramp ||
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
            const AssetRegistry& assetRegistry) {
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
            const std::string cacheKey = binding.assetGuid.value;
            const std::string name =
                "material_ramp/" + cacheKey + "/" +
                ToHex(image.contentHash);
            slot.sourcePath = rampAsset->sourcePath;
            slot.resolvedPath = "generated://" + name;
            if (const auto cached = gRampResourcesByAsset.find(cacheKey);
                cached != gRampResourcesByAsset.end() &&
                cached->second.contentHash == image.contentHash &&
                cached->second.resource) {
                slot.resource = cached->second.resource;
                slot.handle = GetTextureResourceBackendHandle(slot.resource);
                slot.enabled = slot.handle >= 0;
                return slot;
            }
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
                return slot;
            }
            if (auto previous = gRampResourcesByAsset.find(cacheKey);
                previous != gRampResourcesByAsset.end() &&
                previous->second.resource != slot.resource) {
                (void)ReleaseTextureResource(previous->second.resource);
            }
            gRampResourcesByAsset[cacheKey] = CachedRampResource{
                image.contentHash,
                slot.resource
            };
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
                    assetRegistry);
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

        std::optional<uint32_t> ResolveResourceSlot(
            const MaterialProgramResourceBinding& resource,
            uint32_t rampIndex) {
            if (resource.semantic == MaterialResourceSemantic::Ramp) {
                return (std::min)(rampIndex, 1u);
            }
            if (resource.semantic == MaterialResourceSemantic::MatCap) return 2u;
            if (resource.semantic == MaterialResourceSemantic::FaceSdf) return 3u;
            if (resource.semantic == MaterialResourceSemantic::HairShadow) return 7u;
            const std::string id = NormalizeId(resource.resourceId);
            if (id.find("hair") != std::string::npos ||
                id.find("strand") != std::string::npos) return 5u;
            if (id.find("eye") != std::string::npos ||
                id.find("iris") != std::string::npos) return 6u;
            if (resource.semantic == MaterialResourceSemantic::Mask) return 4u;
            // Standard PBR textures are already projected into
            // GpuMaterialData. They must not occupy or overwrite the bounded
            // response-node resource table.
            return std::nullopt;
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
            if (has(MaterialProgramCapability::HairShadow)) flags |= HairShadow;
            if (has(MaterialProgramCapability::SkinLighting)) flags |= TemplateSkin;
            if (has(MaterialProgramCapability::FabricSheen)) flags |= TemplateCloth;
            if (has(MaterialProgramCapability::HybridLighting)) flags |= TemplateHybrid;
            if (has(MaterialProgramCapability::FaceSdf)) flags |= TemplateFace;
            if (has(MaterialProgramCapability::AnisotropicHair)) flags |= TemplateHair;
            if (has(MaterialProgramCapability::EyeLighting)) flags |= TemplateEye;
            for (const MaterialProgramResourceBinding& resource :
                 program.resources) {
                const std::string id = NormalizeId(resource.resourceId);
                if (id.find("haircontrol") != std::string::npos) {
                    flags |= HairControlMap;
                }
                if (id.find("eyecontrol") != std::string::npos) {
                    flags |= EyeControlMap;
                }
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
                MaterialCapabilityBit(MaterialProgramCapability::EyeLighting) |
                MaterialCapabilityBit(MaterialProgramCapability::SkinLighting) |
                MaterialCapabilityBit(MaterialProgramCapability::FabricSheen) |
                MaterialCapabilityBit(MaterialProgramCapability::HybridLighting);
            constexpr uint64_t kHairShadowCapability =
                MaterialCapabilityBit(MaterialProgramCapability::HairShadow);
            return (program.capabilities & kRuntimeResponseCapabilities) != 0u ||
                (program.capabilities & kHairShadowCapability) != 0u ||
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
        ApplyConnectedMaterialResponseParameters(
            program, data.parameters);
        data.resources.resize(kGpuMaterialProgramResourceSlotCount);
        uint32_t rampIndex = 0u;
        for (const MaterialProgramResourceBinding& resource : program.resources) {
            const std::optional<uint32_t> slot =
                ResolveResourceSlot(resource, rampIndex);
            if (resource.semantic == MaterialResourceSemantic::Ramp) {
                ++rampIndex;
            }
            if (!slot.has_value()) {
                continue;
            }
            data.resources[*slot] = BuildResourceSlot(
                resource,
                assetRegistry,
                debugName);
        }
        return data;
    }

    void ResetGeneratedMaterialRuntimeResources() {
        for (const auto& [assetGuid, cached] : gRampResourcesByAsset) {
            (void)assetGuid;
            if (cached.resource) {
                (void)ReleaseTextureResource(cached.resource);
            }
        }
        gRampResourcesByAsset.clear();
    }

    void ResetRuntimeMaterialProgramRegistry() {
        ResetGeneratedMaterialRuntimeResources();
        GetRuntimeMaterialProgramRegistry().Reset();
    }

} // namespace HIKARI::RENDER3D::MATERIAL
