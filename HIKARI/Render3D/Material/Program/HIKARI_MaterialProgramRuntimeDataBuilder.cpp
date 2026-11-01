#include "Render3D/Material/Program/HIKARI_MaterialProgramRuntimeDataBuilder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "RenderContracts/Material/HIKARI_MaterialRampRasterizer.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramStaticEvaluator.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Core/Text/HIKARI_AsciiId.h"
#include "Core/Text/HIKARI_HexString.h"

namespace HIKARI::RENDER3D::MATERIAL {

namespace {
struct CachedRampResource {
    uint64_t contentHash = 0u;
    TextureResourceHandle resource{};
};

// The response head (30) must always fit inside the total GPU block
// (178), and the expression payload must never overlap or exceed the
// same block. These are compile-time layout invariants shared with
// the HLSL evaluator and the GPU block store.
static_assert(kGpuMaterialProgramResponseSlotCount < kGpuMaterialProgramParameterSlotCount);
static_assert(kGpuMaterialExpressionValueBase + kGpuMaterialExpressionValueSlotCount <=
              kGpuMaterialExpressionInstructionBase);
static_assert(kGpuMaterialExpressionInstructionBase + kGpuMaterialExpressionInstructionCount <=
              kGpuMaterialProgramParameterSlotCount);

std::unordered_map<std::string, CachedRampResource> gRampResourcesByAsset{};

std::string NormalizeId(std::string_view id) { return TEXT::NormalizeAsciiId(id); }

TextureResourceColorSpace ResolveColorSpace(MaterialResourceSemantic semantic) {
    return semantic == MaterialResourceSemantic::BaseColor || semantic == MaterialResourceSemantic::Emissive ||
                   semantic == MaterialResourceSemantic::SpecularColor || semantic == MaterialResourceSemantic::Ramp ||
                   semantic == MaterialResourceSemantic::MatCap
               ? TextureResourceColorSpace::Srgb
               : TextureResourceColorSpace::Linear;
}

bool IsRampResource(const MaterialProgramResourceBinding& binding) noexcept {
    return binding.semantic == MaterialResourceSemantic::Ramp || binding.type == MaterialResourceType::Ramp1D ||
           binding.type == MaterialResourceType::Ramp2D;
}

std::string ToHex(uint64_t value) { return TEXT::ToHexString(value); }

RuntimeTextureSlot BuildRampResourceSlot(const MaterialProgramResourceBinding& binding,
                                         const AssetRegistry& assetRegistry) {
    RuntimeTextureSlot slot{};
    slot.texCoord = static_cast<int>(binding.texCoord);
    slot.uvScale = binding.uvScale;
    slot.uvOffset = binding.uvOffset;
    slot.uvRotation = binding.uvRotation;
    const auto* rampAsset = assetRegistry.FindAs<MaterialAssetDescriptor>(binding.assetGuid);
    if (rampAsset == nullptr || rampAsset->sourceData.kind != MaterialAssetKind::Ramp) {
        HIKARI_LOG_WARN("[MaterialProgram] neutral fallback for unresolved Ramp asset: " + binding.assetGuid.value);
        return slot;
    }

    ASSETS::MATERIAL::MaterialRampRasterImage image{};
    std::string error{};
    if (!ASSETS::MATERIAL::RasterizeMaterialRamp(rampAsset->sourceData.ramp, image, error)) {
        HIKARI_LOG_WARN("[MaterialProgram] neutral fallback for invalid Ramp asset " + binding.assetGuid.value + ": " +
                        error);
        return slot;
    }
    const std::string cacheKey = binding.assetGuid.value;
    const std::string name = "material_ramp/" + cacheKey + "/" + ToHex(image.contentHash);
    slot.sourcePath = rampAsset->sourcePath;
    slot.resolvedPath = "generated://" + name;
    if (const auto cached = gRampResourcesByAsset.find(cacheKey); cached != gRampResourcesByAsset.end() &&
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
    slot.resource =
        CreateRgbaTextureResource(name, image.width, image.height, image.rgba8, TextureResourceColorSpace::Linear);
    slot.handle = GetTextureResourceBackendHandle(slot.resource);
    slot.enabled = slot.handle >= 0;
    if (!slot.enabled) {
        HIKARI_LOG_WARN("[MaterialProgram] failed to create GPU Ramp texture: " + binding.assetGuid.value);
        return slot;
    }
    if (auto previous = gRampResourcesByAsset.find(cacheKey);
        previous != gRampResourcesByAsset.end() && previous->second.resource != slot.resource) {
        (void)ReleaseTextureResource(previous->second.resource);
    }
    gRampResourcesByAsset[cacheKey] = CachedRampResource{image.contentHash, slot.resource};
    return slot;
}

RuntimeTextureSlot BuildResourceSlot(const MaterialProgramResourceBinding& binding, const AssetRegistry& assetRegistry,
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
        return BuildRampResourceSlot(binding, assetRegistry);
    }
    const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(binding.assetGuid);
    if (texture == nullptr || texture->sourcePath.empty()) {
        HIKARI_LOG_WARN("[MaterialProgram] neutral fallback for unresolved resource: " + binding.assetGuid.value);
        return slot;
    }
    slot.sourcePath = texture->sourcePath;
    slot.resolvedPath = texture->sourcePath;
    const std::string name = "material_program/" + std::string{debugName} + "/" + binding.resourceId;
    slot.resource =
        binding.type == MaterialResourceType::TextureCube
            ? LoadCubemapResource(name, slot.resolvedPath, ResolveColorSpace(binding.semantic))
            : LoadTextureResourceWithColorSpace(name, slot.resolvedPath, ResolveColorSpace(binding.semantic));
    slot.handle = GetTextureResourceBackendHandle(slot.resource);
    slot.enabled = slot.handle >= 0;
    if (!slot.enabled) {
        HIKARI_LOG_WARN("[MaterialProgram] neutral fallback for failed resource: " + slot.resolvedPath);
    }
    return slot;
}

std::optional<uint32_t> ResolveResourceSlot(const MaterialProgramResourceBinding& resource, uint32_t rampIndex) {
    if (resource.semantic == MaterialResourceSemantic::Ramp) {
        constexpr std::array<uint32_t, 4u> kRampSlots{0u, 1u, 8u, 9u};
        return kRampSlots[(std::min)(rampIndex, static_cast<uint32_t>(kRampSlots.size() - 1u))];
    }
    if (resource.semantic == MaterialResourceSemantic::MatCap)
        return 2u;
    if (resource.semantic == MaterialResourceSemantic::FaceSdf)
        return 3u;
    if (resource.semantic == MaterialResourceSemantic::HairShadow)
        return 7u;
    if (resource.semantic == MaterialResourceSemantic::HairControl)
        return 5u;
    if (resource.semantic == MaterialResourceSemantic::EyeControl)
        return 6u;
    if (resource.semantic == MaterialResourceSemantic::Thickness ||
        resource.semantic == MaterialResourceSemantic::Fabric)
        return 4u;
    const std::string id = NormalizeId(resource.resourceId);
    if (id.find("hair") != std::string::npos || id.find("strand") != std::string::npos)
        return 5u;
    if (id.find("eye") != std::string::npos || id.find("iris") != std::string::npos)
        return 6u;
    if (resource.semantic == MaterialResourceSemantic::Mask)
        return 4u;
    // Noise / DetailNoise / FlowMap は式側の bindingIndex が所有する。
    // 10 以降は式用の範囲なので、応答ノードから二重登録しない。
    // Standard PBR textures are already projected into
    // GpuMaterialData. They must not occupy or overwrite the bounded
    // response-node resource table.
    return std::nullopt;
}

uint32_t BuildFlags(const MaterialProgramIR& program, MaterialProgramResolutionKind resolutionKind) {
    using namespace GPU_MATERIAL_PROGRAM_FLAGS;
    uint32_t flags = 0u;
    if (program.surface.unlit)
        flags |= Unlit;
    if (program.surface.alphaMode == MaterialAlphaMode::Mask)
        flags |= AlphaMask;
    if (program.surface.alphaMode == MaterialAlphaMode::Blend)
        flags |= AlphaBlend;
    if (program.surface.alphaMode == MaterialAlphaMode::Blend &&
        program.surface.alphaRepresentation == MaterialAlphaRepresentation::Premultiplied) {
        flags |= PremultipliedAlphaInput;
    }
    if (program.surface.doubleSided)
        flags |= DoubleSided;
    if (resolutionKind == MaterialProgramResolutionKind::StandardPbrFallback)
        flags |= UsedFallback;
    const auto has = [&](MaterialProgramCapability capability) {
        return (program.capabilities & MaterialCapabilityBit(capability)) != 0u;
    };
    if (has(MaterialProgramCapability::StandardPbr))
        flags |= StandardPbr;
    if (has(MaterialProgramCapability::RampDiffuse))
        flags |= RampDiffuse;
    if (has(MaterialProgramCapability::MultiRampDiffuse))
        flags |= MultiRampDiffuse;
    if (has(MaterialProgramCapability::SpecularBand))
        flags |= SpecularBand;
    if (has(MaterialProgramCapability::FresnelRim))
        flags |= FresnelRim;
    if (has(MaterialProgramCapability::MatCap))
        flags |= MatCap;
    if (has(MaterialProgramCapability::AnisotropicHair))
        flags |= AnisotropicHair;
    if (has(MaterialProgramCapability::FaceSdf))
        flags |= FaceSdf;
    if (has(MaterialProgramCapability::EyeLighting))
        flags |= EyeLighting;
    if (has(MaterialProgramCapability::HairShadow))
        flags |= HairShadow;
    if (has(MaterialProgramCapability::SkinLighting))
        flags |= TemplateSkin;
    if (has(MaterialProgramCapability::FabricSheen))
        flags |= TemplateCloth;
    if (has(MaterialProgramCapability::HybridLighting))
        flags |= TemplateHybrid;
    if (has(MaterialProgramCapability::FaceSdf))
        flags |= TemplateFace;
    if (has(MaterialProgramCapability::AnisotropicHair))
        flags |= TemplateHair;
    if (has(MaterialProgramCapability::CorneaHighlight))
        flags |= CorneaHighlight;
    for (const MaterialProgramResourceBinding& resource : program.resources) {
        const std::string id = NormalizeId(resource.resourceId);
        if (resource.semantic == MaterialResourceSemantic::HairControl || id.find("haircontrol") != std::string::npos) {
            flags |= HairControlMap;
        }
        if (resource.semantic == MaterialResourceSemantic::EyeControl || id.find("eyecontrol") != std::string::npos) {
            flags |= EyeControlMap;
        }
    }
    for (MaterialFeatureRequest request : program.featureRequests) {
        switch (request) {
        case MaterialFeatureRequest::GeometryOutline: flags |= RenderFeatureGeometryOutline; break;
        case MaterialFeatureRequest::ScreenOutline: flags |= RenderFeatureScreenOutline; break;
        case MaterialFeatureRequest::VisibilityOverlay: flags |= RenderFeatureVisibilityOverlay; break;
        case MaterialFeatureRequest::FaceHairShadow: flags |= RenderFeatureFaceHairShadow; break;
        }
    }
    return flags;
}

bool RequiresMaterialProgramBlock(const MaterialProgramIR& program) {
    constexpr uint64_t kRuntimeResponseCapabilities =
        MaterialCapabilityBit(MaterialProgramCapability::RampDiffuse) |
        MaterialCapabilityBit(MaterialProgramCapability::MultiRampDiffuse) |
        MaterialCapabilityBit(MaterialProgramCapability::SpecularBand) |
        MaterialCapabilityBit(MaterialProgramCapability::FresnelRim) |
        MaterialCapabilityBit(MaterialProgramCapability::MatCap) |
        MaterialCapabilityBit(MaterialProgramCapability::AnisotropicHair) |
        MaterialCapabilityBit(MaterialProgramCapability::FaceSdf) |
        MaterialCapabilityBit(MaterialProgramCapability::EyeLighting) |
        MaterialCapabilityBit(MaterialProgramCapability::CorneaHighlight) |
        MaterialCapabilityBit(MaterialProgramCapability::SkinLighting) |
        MaterialCapabilityBit(MaterialProgramCapability::FabricSheen) |
        MaterialCapabilityBit(MaterialProgramCapability::HybridLighting);
    constexpr uint64_t kHairShadowCapability = MaterialCapabilityBit(MaterialProgramCapability::HairShadow);
    return (program.capabilities & kRuntimeResponseCapabilities) != 0u ||
           (program.capabilities & kHairShadowCapability) != 0u || !program.featureRequests.empty() ||
           MaterialProgramHasRuntimeExpression(program);
}
} // namespace

MaterialProgramRuntimeData BuildMaterialProgramRuntimeData(const MaterialProgramIR& program,
                                                           MaterialProgramResolutionKind resolutionKind,
                                                           const AssetRegistry& assetRegistry,
                                                           std::string_view debugName) {
    MaterialProgramRuntimeData data{};
    data.instanceFlags = BuildFlags(program, resolutionKind);
    if (!RequiresMaterialProgramBlock(program)) {
        return data;
    }
    std::string parameterError{};
    if (!BuildMaterialProgramParameterBlock(program, {}, data.parameters, parameterError)) {
        HIKARI_LOG_ERROR("[MaterialProgram] rejected parameter projection: " + parameterError);
        // The registry normally rejects such programs before this point.
        // Keep authored response values as the fail-safe so a bounded
        // stylized material never loses its NPR parameters silently.
        return data;
    }
    data.parameterProjection = BuildMaterialParameterProjectionRecipe(program);
    MaterialExpressionRuntimeProgram expression{};
    std::string expressionError{};
    (void)BuildMaterialExpressionRuntimeProgram(program, expression, expressionError);
    data.resources.resize(kGpuMaterialProgramResourceSlotCount);
    uint32_t rampIndex = 0u;
    for (const MaterialProgramResourceBinding& resource : program.resources) {
        const std::optional<uint32_t> slot = ResolveResourceSlot(resource, rampIndex);
        if (resource.semantic == MaterialResourceSemantic::Ramp) {
            ++rampIndex;
        }
        if (!slot.has_value()) {
            continue;
        }
        data.resources[*slot] = BuildResourceSlot(resource, assetRegistry, debugName);
    }
    for (const uint32_t bindingIndex : expression.resourceBindingIndices) {
        if (bindingIndex >= program.resources.size()) {
            continue;
        }
        const uint32_t slot = kGpuMaterialExpressionResourceBase + bindingIndex;
        if (slot < data.resources.size()) {
            data.resources[slot] = BuildResourceSlot(program.resources[bindingIndex], assetRegistry, debugName);
        }
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

void ResetRuntimeMaterialProgramRegistry(StandardPbrProgramSource source) {
    ResetGeneratedMaterialRuntimeResources();
    GetRuntimeMaterialProgramRegistry().Reset(std::move(source));
}

} // namespace HIKARI::RENDER3D::MATERIAL
