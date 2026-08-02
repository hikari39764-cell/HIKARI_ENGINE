#ifndef HIKARI_MATERIAL_PROGRAM_DATA_INCLUDED
#define HIKARI_MATERIAL_PROGRAM_DATA_INCLUDED

static const uint HIKARI_STANDARD_PBR_MATERIAL_PROGRAM = 0u;
static const uint HIKARI_MATERIAL_PROGRAM_PARAMETER_SLOTS = 20u;
static const uint HIKARI_MATERIAL_PROGRAM_RESOURCE_SLOTS = 8u;
static const uint HIKARI_MATERIAL_PROGRAM_PARAMETER_CAPACITY = 81920u;
static const uint HIKARI_MATERIAL_PROGRAM_RESOURCE_CAPACITY = 32768u;

static const uint HIKARI_PROGRAM_FLAG_STANDARD_PBR = 1u << 6u;
static const uint HIKARI_PROGRAM_FLAG_RAMP_DIFFUSE = 1u << 7u;
static const uint HIKARI_PROGRAM_FLAG_MULTI_RAMP = 1u << 8u;
static const uint HIKARI_PROGRAM_FLAG_SPECULAR_BAND = 1u << 9u;
static const uint HIKARI_PROGRAM_FLAG_FRESNEL_RIM = 1u << 10u;
static const uint HIKARI_PROGRAM_FLAG_MATCAP = 1u << 11u;
static const uint HIKARI_PROGRAM_FLAG_ANISOTROPIC_HAIR = 1u << 12u;
static const uint HIKARI_PROGRAM_FLAG_FACE_SDF = 1u << 13u;
static const uint HIKARI_PROGRAM_FLAG_EYE_LIGHTING = 1u << 14u;
static const uint HIKARI_PROGRAM_FLAG_USES_BLOCK = 1u << 15u;
static const uint HIKARI_PROGRAM_FLAG_TEMPLATE_SKIN = 1u << 17u;
static const uint HIKARI_PROGRAM_FLAG_TEMPLATE_CLOTH = 1u << 21u;
static const uint HIKARI_PROGRAM_FLAG_TEMPLATE_HYBRID = 1u << 22u;
static const uint HIKARI_PROGRAM_FLAG_FEATURE_GEOMETRY_OUTLINE = 1u << 23u;
static const uint HIKARI_PROGRAM_FLAG_FEATURE_SCREEN_OUTLINE = 1u << 24u;
static const uint HIKARI_PROGRAM_FLAG_FEATURE_VISIBILITY_OVERLAY = 1u << 25u;
static const uint HIKARI_PROGRAM_FLAG_FEATURE_FACE_HAIR_SHADOW = 1u << 26u;
static const uint HIKARI_PROGRAM_FLAG_EDITOR_SELECTION_OUTLINE = 1u << 27u;
static const uint HIKARI_PROGRAM_FLAG_HAIR_CONTROL_MAP = 1u << 28u;
static const uint HIKARI_PROGRAM_FLAG_EYE_CONTROL_MAP = 1u << 29u;

static const uint HIKARI_PROGRAM_RESOURCE_RAMP0 = 0u;
static const uint HIKARI_PROGRAM_RESOURCE_RAMP1 = 1u;
static const uint HIKARI_PROGRAM_RESOURCE_MATCAP = 2u;
static const uint HIKARI_PROGRAM_RESOURCE_FACE_SDF = 3u;
static const uint HIKARI_PROGRAM_RESOURCE_MASK = 4u;
static const uint HIKARI_PROGRAM_RESOURCE_HAIR = 5u;
static const uint HIKARI_PROGRAM_RESOURCE_EYE = 6u;
static const uint HIKARI_PROGRAM_RESOURCE_DETAIL = 7u;

StructuredBuffer<float4> gMaterialProgramParameters : register(t0, space6);
StructuredBuffer<uint> gMaterialProgramResources : register(t1, space6);

// Fixed field order shared by every material-program evaluator.
struct HikariMaterialSurfaceData
{
    float3 baseColor;
    float opacity;
    float3 normalWS;
    float metallic;
    float3 viewWS;
    float roughness;
    float3 specularColor;
    float specularFactor;
    float3 emissive;
    float occlusion;
};

// Fixed lobe order lets Hybrid blend direct diffuse/specular/indirect
// independently without changing the material PSO.
struct HikariMaterialLightingResponse
{
    float3 directDiffuse;
    float3 directSpecular;
    float3 indirect;
    float3 additive;
};

bool HikariMaterialProgramUsesBlock(HikariMeshMaterialData materialData)
{
    return materialData.materialProgramIndex !=
            HIKARI_STANDARD_PBR_MATERIAL_PROGRAM &&
        (materialData.instanceFlags & HIKARI_PROGRAM_FLAG_USES_BLOCK) != 0u;
}

float4 HikariLoadMaterialProgramParameter(
    HikariMeshMaterialData materialData,
    uint slot,
    float4 fallbackValue)
{
    // Program0 must return before any program-block index is formed or read.
    if (materialData.materialProgramIndex == HIKARI_STANDARD_PBR_MATERIAL_PROGRAM)
    {
        return fallbackValue;
    }
    if (!HikariMaterialProgramUsesBlock(materialData) ||
        slot >= materialData.parameterBlockCount ||
        slot >= HIKARI_MATERIAL_PROGRAM_PARAMETER_SLOTS)
    {
        return fallbackValue;
    }
    const uint absoluteIndex = materialData.parameterBlockOffset + slot;
    if (absoluteIndex >= HIKARI_MATERIAL_PROGRAM_PARAMETER_CAPACITY)
    {
        return fallbackValue;
    }
    return gMaterialProgramParameters[absoluteIndex];
}

uint HikariLoadMaterialProgramResource(
    HikariMeshMaterialData materialData,
    uint slot)
{
    if (materialData.materialProgramIndex == HIKARI_STANDARD_PBR_MATERIAL_PROGRAM)
    {
        return HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX;
    }
    if (!HikariMaterialProgramUsesBlock(materialData) ||
        slot >= HIKARI_MATERIAL_PROGRAM_RESOURCE_SLOTS)
    {
        return HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX;
    }
    const uint absoluteIndex = materialData.resourceTableOffset + slot;
    if (absoluteIndex >= HIKARI_MATERIAL_PROGRAM_RESOURCE_CAPACITY)
    {
        return HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX;
    }
    const uint descriptorIndex = gMaterialProgramResources[absoluteIndex];
    return descriptorIndex < HIKARI_MATERIAL_TEXTURE_POOL_COUNT
        ? descriptorIndex
        : HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX;
}

#if defined(HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING)
float4 HikariSampleMaterialProgramTexture(
    HikariMeshMaterialData materialData,
    uint resourceSlot,
    SamplerState samplerState,
    float2 uv,
    float4 fallbackValue)
{
    return HikariSampleMaterialTexture(
        HikariLoadMaterialProgramResource(materialData, resourceSlot),
        samplerState,
        uv,
        fallbackValue);
}
#endif

#endif
