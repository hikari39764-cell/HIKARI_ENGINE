#include "Include/Math/HIKARI_BillboardMath.hlsli"
#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Include/HIKARI_MeshMaterialData.hlsli"
#include "Include/Material/HIKARI_MaterialProgramData.hlsli"
#include "Include/Material/HIKARI_MaterialExpression.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"

static const uint HIKARI_PARTICLE_ROTATED_BILLBOARD = 0u;
static const uint HIKARI_PARTICLE_FIXED_Y_AXIS = 1u;
static const uint HIKARI_PARTICLE_DIRECTIONAL_BILLBOARD = 2u;
static const uint HIKARI_PARTICLE_FIXED = 3u;
static const uint HIKARI_PARTICLE_CYLINDRICAL_BILLBOARD = 4u;
static const uint HIKARI_PARTICLE_FACE_CAMERA_POSITION = 5u;
static const uint HIKARI_PARTICLE_MAX_SEGMENTS = 64u;
static const uint HIKARI_PARTICLE_MATERIAL_ALPHA_MASK = 1u << 1u;
static const uint HIKARI_PARTICLE_DEPTH_WORLD_OCCLUDED = 0u;
static const uint HIKARI_PARTICLE_DEPTH_SOFT_INTERSECTION = 1u;
static const uint HIKARI_PARTICLE_DEPTH_CONTACT_SURFACE = 2u;
static const uint HIKARI_PARTICLE_DEPTH_FOREGROUND_ACCENT = 3u;

#include "Include/Vfx/HIKARI_ParticleData.hlsli"

StructuredBuffer<Particle> gDrawParticles : register(t1);
StructuredBuffer<uint> gAliveParticleIndices : register(t2);
Texture2D<float> gParticleSceneDepth : register(t3);
SamplerState gParticleSampler : register(s0);
SamplerState gParticleDepthSampler : register(s1);

#include "Include/Render3D/HIKARI_MeshCameraData.hlsli"
#define gViewProjection gViewProj
#define gInverseViewProjection gInvViewProj
#define gCameraPosition gCameraPos
#define gLinearWrap gParticleSampler
#define gLinearClamp gParticleSampler
#define gSkyPanoramaSampler gParticleSampler
#include "Include/Lighting/HIKARI_ViewLightingData.hlsli"
#include "Include/Lighting/HIKARI_LocalLightList.hlsli"
#include "Include/HIKARI_SkyEnvironmentCommon.hlsli"
Texture2D<float> gShadowMap : register(t0, space9);
SamplerComparisonState gShadowSampler : register(s2);
static const uint HIKARI_FORWARD_COST_NO_SHADOW = 3u;
#include "Include/Forward/HIKARI_ShadowSampling.hlsli"
#include "Include/Lighting/HIKARI_SurfaceDirectLighting.hlsli"
#include "Include/Material/HIKARI_MaterialExpressionSurface.hlsli"

cbuffer ParticleDrawConstants : register(b1)
{
    uint gAliveIndexBase;
    uint gBillboardMode;
    uint gRenderShapeMode;
    uint gSegmentCount;
    float4 gRenderShapeParamsB;
    uint gDepthMode;
    uint gParticleMaterialFlags;
    uint gSceneDepthAvailable;
    uint gParticleReserved;
    float4 gDepthParams;
};

// gDepthParams.z/w = 形状パラメータ 0/1 での幅倍率。
// 形状（ジオメトリ）段だけが読む per-draw パラメータは、ルート定数のコスト
// 上限が厳しいためバッファ側に置き、gParticleReserved (= drawIndex) で引く。
// ピクセル段は上の cbuffer だけで足りる。
struct GpuParticleSpatialEffect { float4 metadata, parameters, parameters2, maskParameters, origin, rotation, scale; };
struct GpuParticleShapeState
{
    float4 renderShapeParamsA;
    float4 renderUvParams;
    float4 renderAnchor;
    uint renderUvMode;
    uint shapeParam0;
    uint shapeParam1;
    uint shapeParam2;
    float bendAmount;
    float twistRadians;
    float shapeParam3;
    float shapeParam4;
    float shapeParam5;
    float billboardAxis;
    uint spatialCount;
    uint spatialReserved;
    GpuParticleSpatialEffect spatialEffects[8];
};
StructuredBuffer<GpuParticleShapeState> gShapeStates : register(t5);
// static: シェーダ内で書き換え可能なスレッドローカルな作業変数。
static GpuParticleShapeState gShapeState;
#define gRenderShapeParamsA gShapeState.renderShapeParamsA
#define gRenderUvParams gShapeState.renderUvParams
#define gRenderAnchor gShapeState.renderAnchor
#define gRenderUvMode gShapeState.renderUvMode
#define gShapeParam0 gShapeState.shapeParam0
#define gShapeParam1 gShapeState.shapeParam1
#define gShapeParam2 gShapeState.shapeParam2
#define gShapeBend gShapeState.bendAmount
#define gShapeTwist gShapeState.twistRadians
#define gShapeParam3 gShapeState.shapeParam3
#define gShapeParam4 gShapeState.shapeParam4
#define gShapeParam5 gShapeState.shapeParam5
#define gBillboardAxis gShapeState.billboardAxis

struct ParticleVertex
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float3 worldPosition : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float3 worldTangent : TEXCOORD3;
    float2 localUv : TEXCOORD4;
    nointerpolation uint flipbookFrame : TEXCOORD5;
    nointerpolation float normalizedAge : TEXCOORD6;
    nointerpolation float randomValue : TEXCOORD7;
    nointerpolation float2 ageAndSpeed : TEXCOORD8;
    // 形状UVへ UV 契約を適用した材質用UV。アトラスセル解決とは独立。
    float2 materialUv : TEXCOORD9;
};

float4 SampleParticleAtlasTexture(
    uint descriptorIndex,
    float2 localUv,
    uint frame,
    uint columns,
    uint rows,
    float4 fallbackValue)
{
    if (!HikariHasMaterialTexture(descriptorIndex))
    {
        return fallbackValue;
    }

    const uint resourceIndex = NonUniformResourceIndex(descriptorIndex);
    uint textureWidth = 1u;
    uint textureHeight = 1u;
    uint mipCount = 1u;
    gMaterialTexturePool[resourceIndex].GetDimensions(
        0u,
        textureWidth,
        textureHeight,
        mipCount);
    const float2 textureSize = max(
        float2(textureWidth, textureHeight),
        1.0f.xx);
    const float2 grid = float2(max(columns, 1u), max(rows, 1u));
    const float2 frameSize = textureSize / grid;
    const float2 atlasDx = ddx(localUv) / grid;
    const float2 atlasDy = ddy(localUv) / grid;
    const float footprint = max(
        dot(atlasDx * textureSize, atlasDx * textureSize),
        dot(atlasDy * textureSize, atlasDy * textureSize));
    const float requestedLod = max(0.5f * log2(max(footprint, 1.0f)), 0.0f);
    const float maximumCellLod = max(
        floor(log2(max(min(frameSize.x, frameSize.y), 1.0f))) - 1.0f,
        0.0f);
    const float lod = min(
        requestedLod,
        min(maximumCellLod, float(max(mipCount, 1u) - 1u)));
    const float2 mipInset = 0.5f * exp2(lod) / textureSize;
    const float2 cell = float2(frame % columns, frame / columns);
    const float2 cellMinimum = cell / grid + mipInset;
    const float2 cellMaximum = (cell + 1.0f.xx) / grid - mipInset;
    const float2 safeUv = lerp(
        cellMinimum,
        max(cellMinimum, cellMaximum),
        saturate(localUv));
    return gMaterialTexturePool[resourceIndex].SampleLevel(
        gParticleSampler,
        safeUv,
        lod);
}

void ResolveBillboardBasis(
    Particle particle,
    out float3 right,
    out float3 up,
    out float3 normal)
{
    float4 centerH = mul(
        gInverseViewProjection,
        float4(0.0f, 0.0f, 0.0f, 1.0f));
    float4 rightH = mul(
        gInverseViewProjection,
        float4(1.0f, 0.0f, 0.0f, 1.0f));
    float4 upH = mul(
        gInverseViewProjection,
        float4(0.0f, 1.0f, 0.0f, 1.0f));
    centerH.xyz /= max(abs(centerH.w), 1.0e-6f);
    rightH.xyz /= max(abs(rightH.w), 1.0e-6f);
    upH.xyz /= max(abs(upH.w), 1.0e-6f);
    const float3 cameraRight = NormalizeOr(
        rightH.xyz - centerH.xyz,
        float3(1.0f, 0.0f, 0.0f));
    const float3 cameraUp = NormalizeOr(
        upH.xyz - centerH.xyz,
        float3(0.0f, 1.0f, 0.0f));
    const float3 cameraForward = NormalizeOr(
        cross(cameraRight, cameraUp),
        float3(0.0f, 0.0f, 1.0f));
    const float3 toCamera = NormalizeOr(
        gCameraPosition.xyz - particle.positionAge.xyz,
        -cameraForward);

    right = cameraRight;
    up = cameraUp;
    normal = toCamera;
    if (gBillboardMode == HIKARI_PARTICLE_FIXED)
    {
        const float4 orientation = NormalizeQuaternion(particle.orientation);
        right = NormalizeOr(
            RotateByQuaternion(float3(1.0f, 0.0f, 0.0f), orientation),
            cameraRight);
        up = NormalizeOr(
            RotateByQuaternion(float3(0.0f, 1.0f, 0.0f), orientation),
            cameraUp);
        normal = NormalizeOr(-cross(right, up), toCamera);
    }
    else if (gBillboardMode == HIKARI_PARTICLE_FIXED_Y_AXIS)
    {
        up = float3(0.0f, 1.0f, 0.0f);
        normal = NormalizeOr(
            float3(toCamera.x, 0.0f, toCamera.z),
            float3(0.0f, 0.0f, -1.0f));
        right = NormalizeOr(cross(normal, up), cameraRight);
    }
    else if (gBillboardMode == HIKARI_PARTICLE_DIRECTIONAL_BILLBOARD)
    {
        const float3 projectedDirection = particle.velocityLifetime.xyz -
            normal * dot(particle.velocityLifetime.xyz, normal);
        up = NormalizeOr(projectedDirection, cameraUp);
        right = NormalizeOr(cross(normal, up), cameraRight);
    }
    else if (gBillboardMode == HIKARI_PARTICLE_CYLINDRICAL_BILLBOARD)
    {
        // Look At Line: 選んだローカル軸は形状の向きのまま固定し、法線だけを
        // 常にカメラへ向ける。帯の幅はその法線から導かれるため、ビームは
        // どの角度から見ても幅が正面を向く。軸は形状ごとではなくパラメータ。
        const float4 orientation = NormalizeQuaternion(particle.orientation);
        const uint axisIndex = (uint)clamp(gBillboardAxis, 0.0f, 2.0f);
        const float3 localAxis = axisIndex == 1u
            ? float3(0.0f, 1.0f, 0.0f)
            : axisIndex == 2u ? float3(0.0f, 0.0f, 1.0f)
                              : float3(1.0f, 0.0f, 0.0f);
        const float3 axis = NormalizeOr(
            RotateByQuaternion(localAxis, orientation),
            cameraRight);
        const float3 viewPerpendicular = toCamera -
            axis * dot(toCamera, axis);
        normal = NormalizeOr(viewPerpendicular, cameraUp);
        up = NormalizeOr(-cross(normal, axis), cameraUp);
        right = axis;
    }
    else if (gBillboardMode == HIKARI_PARTICLE_FACE_CAMERA_POSITION)
    {
        // 面をカメラ平面ではなくカメラ位置へ正対させる。ワールド上方向を
        // 基準に up を取るため、画面端でも傾きが破綻しない。
        normal = toCamera;
        const float3 worldUp = float3(0.0f, 1.0f, 0.0f);
        const float3 projectedUp = worldUp - normal * dot(worldUp, normal);
        up = NormalizeOr(projectedUp, cameraUp);
        right = NormalizeOr(cross(normal, up), cameraRight);
    }
    else
    {
        const float sine = sin(particle.scaleRotation.z);
        const float cosine = cos(particle.scaleRotation.z);
        right = NormalizeOr(
            cameraRight * cosine + cameraUp * sine,
            cameraRight);
        up = NormalizeOr(
            cameraRight * -sine + cameraUp * cosine,
            cameraUp);
        normal = NormalizeOr(-cross(right, up), normal);
    }
}

#include "Include/Vfx/HIKARI_ParticleSpatialDeformation.hlsli"
#include "Include/Vfx/HIKARI_ParticleGeometry.hlsli"

float HikariResolveParticleDepthFade(ParticleVertex input)
{
    if (gDepthMode != HIKARI_PARTICLE_DEPTH_SOFT_INTERSECTION ||
        gSceneDepthAvailable == 0u)
    {
        return 1.0f;
    }
    const float2 uv = HikariScreenUvFromSvPosition(
        input.position,
        gScreenParams.zw);
    if (!HikariIsScreenUvInsideTexelGuard(uv, gScreenParams.zw))
    {
        return 1.0f;
    }
    const float sceneDepth = gParticleSceneDepth.SampleLevel(
        gParticleDepthSampler,
        uv,
        0.0f);
    float3 sceneWorldPosition = 0.0f.xxx;
    if (!HikariReconstructWorldPosition(
            uv,
            sceneDepth,
            gInverseViewProjection,
            sceneWorldPosition))
    {
        return 1.0f;
    }
    const float separation = distance(
        sceneWorldPosition,
        input.worldPosition);
    return saturate(
        separation / max(gDepthParams.x, 1.0e-3f));
}

#include "Include/Lighting/HIKARI_SimpleSurfaceLighting.hlsli"

float4 ParticlePS(ParticleVertex input, bool frontFace : SV_IsFrontFace) : SV_TARGET
{
    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(gMaterialDataIndex);
    float4 materialColor = materialData.baseColor;
    const bool lit = (gParticleMaterialFlags & 1u) != 0u;
    const float3 geometricNormal = normalize(input.worldNormal) * (frontFace ? 1.0f : -1.0f);
    const float4 tangent = float4(normalize(input.worldTangent),1.0f);
    HikariMaterialPbrInputs pbrInputs = (HikariMaterialPbrInputs)0;
    float3 shadingNormal = geometricNormal;
    if (lit)
    {
        pbrInputs = HikariResolveMaterialPbrInputs(materialData,input.uv,input.uv,gParticleSampler);
        shadingNormal = HikariResolveSurfaceShadingNormal(materialData,geometricNormal,tangent,input.uv,input.uv,gParticleSampler);
    }
    float opacity = 1.0f;
    float3 emissive = materialData.emissiveFactor.rgb *
        materialData.emissiveFactor.w;
    float alphaClipThreshold = 0.001f;
    const uint flipbookGrid = asuint(gRenderShapeParamsB.z);
    const uint columns = max(flipbookGrid & 0xffu, 1u);
    const uint rows = max((flipbookGrid >> 8u) & 0xffu, 1u);
    const bool hasMaterialExpression =
        materialData.materialProgramIndex !=
        HIKARI_STANDARD_PBR_MATERIAL_PROGRAM;
    if (hasMaterialExpression)
    {
        HikariMaterialExpressionContext context = HikariBuildMaterialExpressionContext(
            input.uv,input.localUv,gTimeParams,input.worldPosition,geometricNormal,tangent,
            gCameraPosition.xyz,input.position,gScreenParams.zw);
        context.particleState = float4(input.normalizedAge, input.ageAndSpeed.x,
            input.randomValue, input.ageAndSpeed.y);
        context.particleColor = input.color;
        context.particleUv = input.materialUv;
        const HikariMaterialExpressionResult expression =
            HikariEvaluateMaterialExpression(
                materialData,
                context,
                gParticleSampler);
        if (lit)
        {
            HikariApplyMaterialExpressionPbr(expression,pbrInputs);
            shadingNormal = HikariResolveMaterialExpressionNormal(expression,materialData.normalScale,
                geometricNormal,tangent,shadingNormal);
        }
        if ((expression.outputMask &
             HIKARI_EXPRESSION_OUTPUT_BASE_COLOR) != 0u)
        {
            materialColor = expression.baseColor;
        }
        if ((expression.outputMask &
             HIKARI_EXPRESSION_OUTPUT_OPACITY) != 0u)
        {
            opacity = expression.opacity;
        }
        if ((expression.outputMask &
             HIKARI_EXPRESSION_OUTPUT_EMISSIVE) != 0u)
        {
            emissive = expression.emissive;
        }
        if ((expression.outputMask &
             HIKARI_EXPRESSION_OUTPUT_ALPHA_CLIP) != 0u)
        {
            alphaClipThreshold = max(
                expression.alphaClipThreshold,
                0.0f);
        }
    }
    else
    {
        const float2 baseColorUv = HikariResolveMaterialUv(
            materialData,
            HIKARI_MATERIAL_UV_BASE_COLOR,
            input.uv,
            input.uv);
        if (materialData.hasBaseColorTexture != 0u)
        {
            materialColor *= SampleParticleAtlasTexture(
                materialData.baseColorTextureDescriptorIndex,
                columns > 1u || rows > 1u
                    ? input.localUv
                    : baseColorUv,
                input.flipbookFrame,
                columns,
                rows,
                1.0f.xxxx);

        }
        if (materialData.hasEmissiveTexture != 0u)
        {
            const float2 emissiveUv = HikariResolveMaterialUv(
                materialData,
                HIKARI_MATERIAL_UV_EMISSIVE,
                input.uv,
                input.uv);
            emissive *= HikariSampleMaterialTexture(
                materialData.emissiveTextureDescriptorIndex,
                gParticleSampler,
                emissiveUv,
                1.0f.xxxx).rgb;
        }
        if ((materialData.materialFlags &
             HIKARI_PARTICLE_MATERIAL_ALPHA_MASK) != 0u)
        {
            alphaClipThreshold = max(materialData.pbrParams.w, 0.0f);
        }
    }
    const float4 layerColor = (gParticleMaterialFlags & 2u) != 0u ? 1.0f.xxxx : input.color;
    float alpha = layerColor.a * opacity;
    if (!hasMaterialExpression)
    {
        alpha *= materialColor.a;
    }

    alpha *= HikariResolveParticleDepthFade(input);
    clip(alpha - alphaClipThreshold);
    if ((gParticleMaterialFlags & 4u) != 0u)
    {
        // 歪み層：戦闘の画面歪み遮罩へ書く。RG = 画面 UV のずれ、B = 覆い、A = ぼかし。
        // 層色の R が強さ（最大で画面の 6%）、G がぼかし。B が 0.5 以上なら底色貼図の RG を
        // 法線場として使い、未満なら不透明度の縁に沿って内へ屈折する
        const float strength = saturate(input.color.r) * 0.06f;
        float2 field = 0.0f.xx;
        if (input.color.b >= 0.5f)
        {
            field = materialColor.rg * 2.0f - 1.0f;
        }
        else
        {
            const float2 gradient = float2(ddx(alpha), ddy(alpha));
            const float gradientLength = length(gradient);
            if (gradientLength > 1.0e-5f)
            {
                field = -gradient / gradientLength * saturate(gradientLength * 6.0f);
            }
        }
        // 画面の V は下向きなので法線場の Y を反転する
        return float4(field * float2(1.0f, -1.0f) * strength * alpha, alpha, alpha * saturate(input.color.g));
    }
    const float3 baseColor = layerColor.rgb * materialColor.rgb;
    // 受光と発光は絶対量、Unlit の色は表示参照。どちらもこのビューの HDR 空間へ移して書く。
    const float3 straightColor = (lit ? HikariShadeSimpleSurface(baseColor,pbrInputs.metallic,
        pbrInputs.roughness,pbrInputs.specularColor,pbrInputs.specularFactor,pbrInputs.occlusion,
        shadingNormal,geometricNormal,input.worldPosition,input.position.xy)
        : HikariDisplayReferredToRadiance(baseColor, gViewExposure)) + emissive;
    return float4(HikariPreExpose(straightColor, gViewExposure) * alpha, alpha);
}
