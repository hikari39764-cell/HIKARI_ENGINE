#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/Material/HIKARI_MaterialExpressionSurface.hlsli"
#include "Include/Material/HIKARI_MaterialBaseColorInputs.hlsli"
#include "Include/HIKARI_PbrCommon.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Surface/HIKARI_MaterialShell.hlsli"

cbuffer TestCase : register(b0)
{
    uint testMode;
    uint parameterOffset;
    uint parameterCount;
    uint resourceOffset;
};
SamplerState materialSampler : register(s0);

// 光源取得と影だけを固定し、製品の Forward 集積ループをそのまま PS で通す。
#include "Include/Lighting/HIKARI_LocalLightContract.hlsli"
static HikariLocalLightGpuData gHikariLocalLights[1];
static const uint gHikariLocalLightClusterIndices[1] = {0u};
static uint shadowReads = 0u;
float HikariCameraViewDepth(float3 position) { return 1.0f; }
uint2 HikariResolveLocalLightCluster(float2 pixel, float depth) { return uint2(0u, 1u); }
float SampleLocalLightVisibility(HikariLocalLightGpuData light, float3 position, float3 normal, uint receive)
{
    ++shadowReads;
    return 0.37f;
}
#define gLinearClamp materialSampler
#include "Include/Lighting/HIKARI_SurfaceDirectLighting.hlsli"
#undef gLinearClamp

float4 VSMain(uint vertex : SV_VertexID) : SV_POSITION
{
    const float2 uv = float2((vertex << 1u) & 2u, vertex & 2u);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0.25f, 1);
}

struct PixelResult
{
    float4 surface : SV_Target0;
    float4 color : SV_Target1;
    float4 normalAndOcclusion : SV_Target2;
    float4 specular : SV_Target3;
};

PixelResult PSMain(float4 position : SV_POSITION)
{
    HikariMeshMaterialData material = HikariBuildDefaultMeshMaterialData();
    material.materialProgramIndex = (testMode > 0u && testMode < 4u) || testMode >= 9u ? 1u : 0u;
    material.instanceFlags = HIKARI_PROGRAM_FLAG_USES_BLOCK;
    material.parameterBlockCount = parameterCount;
    material.parameterBlockOffset = parameterOffset;
    material.resourceTableOffset = resourceOffset;
    if (testMode == 17u)
    {
        HikariLocalLightGpuData light = (HikariLocalLightGpuData)0;
        light.positionRange = HikariLoadMaterialProgramParameter(material, 0u, 0.0f.xxxx);
        light.directionInnerCos = HikariLoadMaterialProgramParameter(material, 1u, 0.0f.xxxx);
        light.shapeTangent = HikariLoadMaterialProgramParameter(material, 2u, 0.0f.xxxx);
        light.colorIntensity = HikariLoadMaterialProgramParameter(material, 3u, 0.0f.xxxx);
        const float4 settings = HikariLoadMaterialProgramParameter(material, 4u, 0.0f.xxxx);
        light.kind = (uint)settings.x;
        light.outerConeCos = settings.y;
        light.sourceRadius = settings.z;
        light.angularFalloff = settings.w;
        gHikariLocalLights[0] = light;
        const float4 normalRoughness = HikariLoadMaterialProgramParameter(material, 5u, 0.0f.xxxx);
        const float4 viewMetallic = HikariLoadMaterialProgramParameter(material, 6u, 0.0f.xxxx);
        const float3 normal = normalize(normalRoughness.xyz);
        const HikariPbrShadingContext context = HikariBuildPbrShadingContext(
            float3(0.6f, 0.3f, 0.1f), viewMetallic.w, normalRoughness.w, 1.0f.xxx, 0.5f,
            normal, normalize(viewMetallic.xyz));
        const float3 world = float3((position.xy - 8.0f) * 0.08f, 0.0f);
        const HikariPbrPixelFootprint footprint = HikariBuildPbrPixelFootprint(context, world);
        // 早期棄却のない従来の評価を参照とし、面光源の別々の代表点も両方比較する。
        HikariDirectPbrLobes reference = (HikariDirectPbrLobes)0;
        HikariLocalLightSample sample;
        if (HikariResolveLocalLightSample(light, world, reflect(-context.view, normal), context.roughness, sample))
        {
            HikariApplyLocalLightTextures(light, sample);
            reference = HikariEvaluateDirectPbrLobes(HikariFilterLocalPbrContext(context, footprint,
                sample.directionToLight, sample.distanceToLight, sample.sourceHalfAngleSineSquared),
                sample.directionToLight, sample.radiance);
            if (any(sample.specularDirectionToLight != sample.directionToLight))
                reference.specular = HikariEvaluateDirectPbrLobes(HikariFilterLocalPbrContext(context, footprint,
                    sample.specularDirectionToLight, sample.distanceToLight, sample.sourceHalfAngleSineSquared),
                    sample.specularDirectionToLight, sample.specularRadiance).specular;
        }
        reference.diffuse *= 0.37f;
        reference.specular *= 0.37f;
        const HikariDirectPbrLobes actual = AccumulateLocalLightPbrLobes(context, world, position.xy, normal, 1u);
        const uint expectedReads = any(reference.diffuse != 0.0f) || any(reference.specular != 0.0f) ? 1u : 0u;
        PixelResult result = (PixelResult)0;
        result.surface = float4(abs(actual.diffuse - reference.diffuse) + abs(actual.specular - reference.specular),
            shadowReads == expectedReads ? 1.0f : 0.0f);
        return result;
    }
    if (testMode == 16u)
    {
        const float4 settings = HikariLoadMaterialProgramParameter(material, 0u, 0.0f.xxxx);
        const float4 bounds = HikariLoadMaterialProgramParameter(material, 1u, 0.0f.xxxx);
        const float4 control = HikariLoadMaterialProgramParameter(material, 2u, 0.0f.xxxx);
        const float4x4 viewProj = float4x4(
            HikariLoadMaterialProgramParameter(material, 3u, 0.0f.xxxx),
            HikariLoadMaterialProgramParameter(material, 4u, 0.0f.xxxx),
            HikariLoadMaterialProgramParameter(material, 5u, 0.0f.xxxx),
            HikariLoadMaterialProgramParameter(material, 6u, 0.0f.xxxx));
        const float amount = HikariShellLodAmount(settings, bounds, viewProj, control.y);
        const uint count = HikariShellLayerCount(settings, amount);
        PixelResult result = (PixelResult)0;
        result.surface = float4(amount, count, 0.0f, 1.0f);
        result.color = HikariShellLayer((uint)control.x, count, amount);
        return result;
    }
    material.pbrParams = float4(0.2f, 0.8f, 1.0f, 0.5f);
    material.baseColor = float4(0.2f, 0.3f, 0.4f, 1.0f);
    if (testMode >= 4u && testMode < 9u)
    {
        material.baseColor.a = testMode == 4u ? 0.25f : testMode == 5u ? 1.0f : 0.5f;
        material.hasBaseColorTexture = testMode == 5u || testMode == 6u || testMode == 8u;
        material.baseColorTextureDescriptorIndex = testMode == 5u ? 0xffffffffu : 0u;
        if (testMode == 6u) material.uvSet0.x = 1u;
        if (testMode == 6u || testMode == 8u) material.pbrParams.w = 0.25f;
        if (testMode == 8u)
        {
            material.baseColorUvTransform.zw = 1.0f.xx;
            material.uvRotation0.x = HIKARI_PI;
        }
    }
    const float3 geometricNormal = testMode == 3u ? float3(0, 0, -1) : float3(0, 0, 1);
    const float4 tangent = float4(1, 0, 0, 1);
    const float2 uv = position.xy / 16.0f;
    HikariMaterialExpressionContext context = HikariBuildMaterialExpressionContext(
        uv, uv.yx, float4(1.75f, 0.5f, 0.25f, 0.125f), float3(position.xy, 2),
        geometricNormal, tangent, float3(8, 8, 10), position, (1.0f / 16.0f).xx);
    if (testMode == 15u) context.shellLayer = float4(0.75f, 0.0f, 6.0f, 0.8f);
    const HikariMaterialExpressionResult expression =
        HikariEvaluateMaterialExpression(material, context, materialSampler);
    float4 color = HikariResolveMaterialBaseColor(material, uv, uv.yx, materialSampler);
    float cutoff = material.pbrParams.w;
    HikariApplyMaterialExpressionCoverage(expression, color, cutoff);
    if (color.a < cutoff) discard;
    HikariMaterialPbrInputs inputs = HikariResolveMaterialPbrInputs(material, uv, uv.yx, materialSampler);
    HikariApplyMaterialExpressionPbr(expression, inputs);
    const float3 normal = HikariResolveMaterialExpressionNormal(
        expression, testMode == 2u ? 2.0f : 1.0f, geometricNormal, tangent, geometricNormal);
    const HikariPbrShadingContext pbr = HikariBuildPbrShadingContext(
        color.rgb, inputs.metallic, inputs.roughness, inputs.specularColor,
        inputs.specularFactor, normal, context.viewDirection);
    PixelResult result;
    result.surface = HikariPackSceneSurface(normal, pbr.roughness, inputs.metallic);
    result.color = color;
    result.normalAndOcclusion = float4(normal, inputs.occlusion);
    result.specular = float4(inputs.specularColor, inputs.specularFactor);
    if (testMode == 10u)
    {
        result.color = HikariLoadMaterialProgramParameter(material, 30u, color);
        result.specular.w = HikariLoadMaterialProgramResource(material, 10u) ==
            HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX ? 1.0f : 0.0f;
    }
    return result;
}
