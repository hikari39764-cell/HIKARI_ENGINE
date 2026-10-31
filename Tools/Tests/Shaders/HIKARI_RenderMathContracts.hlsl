#include "../../../HIKARI/Shaders/Include/Math/HIKARI_Noise.hlsli"
#include "../../../HIKARI/Shaders/Include/Lighting/HIKARI_LocalFogDensity.hlsli"
#include "../../../HIKARI/Shaders/Include/HIKARI_PbrCommon.hlsli"
#include "../../../HIKARI/Shaders/Include/Material/HIKARI_PbrMaterialPolicy.hlsli"
#include "../../../HIKARI/Shaders/Include/Lighting/HIKARI_LocalLightContract.hlsli"
#include "../../../HIKARI/Shaders/Include/Surface/HIKARI_SurfaceTangentFrame.hlsli"
#include "../../../HIKARI/Shaders/Include/Sky/HIKARI_SkyRadiance.hlsli"
#include "../../../HIKARI/Shaders/Include/Sky/HIKARI_VolumetricCloud.hlsli"
#include "../../../HIKARI/Shaders/Include/Lighting/HIKARI_VolumetricCamera.hlsli"
#include "../../../HIKARI/Shaders/Include/Lighting/HIKARI_VolumetricScattering.hlsli"
#include "../../../HIKARI/Shaders/Include/Math/HIKARI_BillboardMath.hlsli"

// 開いた格子と、壁で分かれた格子を使い、製品の問い合わせを GPU 上で直接検証する。
#define HIKARI_MATERIAL_TEXTURE_POOL_COUNT 3
Texture2D<float4> gMaterialTexturePool[3] : register(t4);
SamplerState gLinearClamp : register(s1);
#include "../../../HIKARI/Shaders/Include/Lighting/HIKARI_DynamicGiSampling.hlsli"

RWStructuredBuffer<float4> results : register(u0);

[numthreads(1, 1, 1)]
void Main(uint3 id : SV_DispatchThreadID)
{
    const float roughness = float(id.y) / 20.0f;
    if (id.y == 13u)
    {
        // 点評価を独立した参照にして、区間の棄却で非ゼロの照明を失わないことを確かめる。
        HikariLocalLightGpuData light = (HikariLocalLightGpuData)0;
        light.positionRange = float4(id.x == 20u ? float3(100000, -80000, 60000) : 0.0f.xxx, 10.0f);
        light.directionInnerCos = float4(normalize(float3(0.1f * float(id.x), 0.2f, 1.0f)), 0.9999f);
        light.outerConeCos = 0.1f + 0.044f * float(id.x);
        light.angularFalloff = 1.0f;
        light.colorIntensity = 1.0f.xxxx;
        light.kind = id.x & 1u;
        float lost = 0, rejected = 0, lit = 0;
        for (uint trial = 0u; trial < 128u; ++trial)
        {
            const float t = float(trial);
            const float3 center = light.positionRange.xyz + float3(
                18.0f * sin(t * 1.7f), 13.0f * cos(t * 0.31f), 16.0f * sin(t * 0.63f));
            const float3 halfVector = trial % 7u == 0u ? 0.0f.xxx :
                float3(6.0f * cos(t * 1.3f), 5.0f * sin(t * 0.73f), 4.0f);
            const bool keep = HikariLocalLightIntersectsSegment(light, center, length(halfVector));
            rejected += keep ? 0.0f : 1.0f;
            for (uint step = 0u; step <= 64u; ++step)
            {
                HikariLocalLightSample sample;
                const bool contributes = HikariResolveLocalLightSample(
                    light, center + halfVector * (float(step) / 32.0f - 1.0f), sample);
                lit += contributes ? 1.0f : 0.0f;
                lost += contributes && !keep ? 1.0f : 0.0f;
            }
        }
        light.kind = HIKARI_LOCAL_LIGHT_TYPE_SPOT;
        light.positionRange.xyz = 0.0f.xxx;
        light.directionInnerCos.xyz = float3(0, 0, 1);
        light.outerConeCos = 0.97f;
        // 中点は光錐外でも端点が中にある区間、頂点、range/光錐の接線は保守的に残す。
        const bool boundary = HikariLocalLightIntersectsSegment(light, float3(-4, 0, 3), 4.0f) &&
            HikariLocalLightIntersectsSegment(light, 0.0f.xxx, 0.0f) &&
            HikariLocalLightIntersectsSegment(light, float3(0, 0, 10), 0.0f) &&
            HikariLocalLightIntersectsSegment(light, float3(sqrt(1.0f-0.97f*0.97f), 0, 0.97f) * 5.0f, 0.0f);
        results[1417u + id.x] = float4(lost, rejected, lit, boundary ? 1.0f : 0.0f);
    }
    if (id.y==8u)
    {
        const float g=(float(id.x)-10.0f)*.08f;
        float integral=0;
        for(uint sample=0u;sample<8192u;++sample)
            integral+=HikariHenyeyGreenstein(-1.0f+(float(sample)+.5f)*2.0f/8192.0f,g)*(12.56637061436f/8192.0f);
        results[1201u+id.x*3u]=float4(integral,HikariHenyeyGreenstein(1,g),HikariHenyeyGreenstein(-1,g),g);
        results[1202u+id.x*3u]=float4(
            HikariSingleScatteringSource(.5f.xxx,0.0f.xxx,0.0f.xxx,1,1,g).x,
            HikariSingleScatteringSource(.5f.xxx,.25f.xxx,2.0f.xxx,.5f,.7f,g).x,
            HikariSingleScatteringSource(.5f.xxx,.25f.xxx,4.0f.xxx,.5f,.7f,g).x,
            HikariSingleScatteringSource(.5f.xxx,.25f.xxx,4.0f.xxx,0,.7f,g).x);
        // 雲の位相関数は異方性の倍率によらず全球で 1 に正規化され、主光源からの散乱光は光学的厚さとともに単調に減る。
        const float lobeScale=float(id.x)/20.0f;
        float cloudIntegral=0;
        for(uint cloudSample=0u;cloudSample<8192u;++cloudSample)
            cloudIntegral+=HikariCloudPhase(-1.0f+(float(cloudSample)+.5f)*2.0f/8192.0f,lobeScale)*(12.56637061436f/8192.0f);
        results[1203u+id.x*3u]=float4(cloudIntegral,HikariCloudLightScattering(.3f,0,1),
            HikariCloudLightScattering(.3f,4,1),HikariCloudLightScattering(.3f,1000,1));
    }
    if (id.y == 9u)
    {
        const float tau=pow(10.0f,-8.0f+float(id.x)*0.6f);
        results[1264u+id.x]=float4(HikariExtinctionIntegral(float3(tau,0,2*tau)/3.0f,3.0f),tau);
    }
    if (id.y == 10u && id.x < 4u)
    {
        const float2 sourceUvs[4] = {
            float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f), float2(0.2f, 0.7f)
        };
        const float2 sourceUv = sourceUvs[id.x];
        results[1285u + id.x] = float4(
            HikariMapPackedSpriteUv(sourceUv, true, false), HikariMapPackedSpriteUv(sourceUv, false, false));
        results[1308u + id.x] = float4(HikariMapPackedSpriteUv(sourceUv, true, true), 0.0f, 0.0f);
        const float3 tangentNormals[2] = {
            normalize(float3(0.6f, 0.0f, 0.8f)), normalize(float3(0.0f, 0.6f, 0.8f))
        };
        results[1289u + id.x] = float4(
            HikariResolveWorldSpriteNormal(
                tangentNormals[id.x & 1u], float3(1, 0, 0), float3(0, 0, 1), true, false), 1.0f);
        if (id.x == 0u) {
            results[1293u] = float4(
                HikariResolveWorldSpriteNormal(
                    tangentNormals[0], float3(1, 0, 0), float3(0, 0, 1), false, false), 1.0f);
            results[1312u] = float4(
                HikariResolveWorldSpriteNormal(
                    tangentNormals[0], float3(1, 0, 0), float3(0, 0, 1), true, true), 1.0f);
            results[1313u] = float4(
                HikariResolveWorldSpriteNormal(
                    tangentNormals[0], float3(1, 0, 0), float3(0, 0, 1), false, true), 1.0f);
        }
    }
    if (id.y == 11u && id.x == 0u)
    {
        const float3 cameraPosition = float3(3.0f, -2.0f, 5.0f);
        const float3 authoredForward = normalize(float3(0.36f, 0.48f, -0.8f));
        const float3 cameraRight = normalize(cross(authoredForward, float3(0.0f, 1.0f, 0.0f)));
        const float3 cameraUp = normalize(cross(cameraRight, authoredForward));
        const float3 cameraBack = -authoredForward;
        const float scaleX = 0.25f;
        const float scaleY = 0.5f;
        const float depthScale = -1.0f / 99.9f;
        const float depthOffset = -0.1f / 99.9f;
        const float3 previousCameraPosition = cameraPosition + float3(-0.4f, 0.2f, 0.3f);
        const float4x4 inverseViewProjection = float4x4(
            float4(cameraRight.x / scaleX, cameraUp.x / scaleY, cameraBack.x / depthScale,
                cameraPosition.x - cameraBack.x * depthOffset / depthScale),
            float4(cameraRight.y / scaleX, cameraUp.y / scaleY, cameraBack.y / depthScale,
                cameraPosition.y - cameraBack.y * depthOffset / depthScale),
            float4(cameraRight.z / scaleX, cameraUp.z / scaleY, cameraBack.z / depthScale,
                cameraPosition.z - cameraBack.z * depthOffset / depthScale),
            float4(0.0f, 0.0f, 0.0f, 1.0f));
        const float4x4 previousViewProjection = float4x4(
            float4(scaleX * cameraRight, -scaleX * dot(cameraRight, previousCameraPosition)),
            float4(scaleY * cameraUp, -scaleY * dot(cameraUp, previousCameraPosition)),
            float4(depthScale * cameraBack,
                depthOffset - depthScale * dot(cameraBack, previousCameraPosition)),
            float4(0.0f, 0.0f, 0.0f, 1.0f));
        const float2 uvA = float2(0.15f, 0.3f);
        const float2 uvB = float2(0.82f, 0.73f);
        float3 originA;
        float3 rayA;
        bool orthographicA;
        HikariReconstructVolumeRay(
            uvA, inverseViewProjection, cameraPosition, originA, rayA, orthographicA);
        float3 originB;
        float3 rayB;
        bool orthographicB;
        HikariReconstructVolumeRay(
            uvB, inverseViewProjection, cameraPosition, originB, rayB, orthographicB);
        const float3 cameraForward = HikariVolumeCameraForward(
            inverseViewProjection, cameraPosition, orthographicA);
        const float3 sampleA = originA + rayA * 7.0f;
        const float3 sampleB = originB + rayB * 7.0f;
        const bool previousOrthographic = HikariIsOrthographic(previousViewProjection);
        const float3 previousForward = previousOrthographic
            ? normalize(previousViewProjection[2].xyz)
            : 0.0f.xxx;
        results[1294u] = float4(
            orthographicA ? 1.0f : 0.0f,
            HikariOrthographicForwardFromInverseViewProjection(inverseViewProjection));
        results[1295u] = float4(rayA, HikariVolumeViewDepth(sampleA, cameraPosition, cameraForward));
        results[1296u] = float4(rayB, HikariVolumeViewDepth(sampleB, cameraPosition, cameraForward));
        results[1297u] = float4(originA, dot(originA - cameraPosition, cameraForward));
        results[1298u] = float4(originB, dot(originB - cameraPosition, cameraForward));
        results[1299u] = float4(sampleA, HikariVolumeCameraDistance(
            sampleA, cameraPosition, cameraForward, orthographicA));
        results[1300u] = float4(sampleB, HikariVolumeCameraDistance(
            sampleB, cameraPosition, cameraForward, orthographicB));
        results[1301u] = float4(
            previousOrthographic ? 1.0f : 0.0f, previousForward);
        results[1302u] = float4(
            HikariVolumeCameraDistance(sampleA, previousCameraPosition, previousForward, previousOrthographic),
            HikariVolumeViewDepth(sampleA, previousCameraPosition, previousForward),
            length(sampleA - previousCameraPosition),
            0.0f);
    }
    if (id.y == 12u && id.x == 0u)
    {
        const float4x4 shadowViewProj = float4x4(
            float4(1.25f, 0.08f, -0.03f, 0.0f),
            float4(-0.07f, 1.4f, 0.04f, 0.0f),
            float4(0.02f, -0.05f, -1.001f, -0.1001f),
            float4(0.0f, 0.0f, -1.0f, 0.0f));
        const float3 worldPosition = float3(0.7f, -0.3f, -6.0f);
        const float3 normalWS = normalize(float3(0.3f, 0.8f, -0.5f));
        const float4 clipPosition = mul(shadowViewProj, float4(worldPosition, 1.0f));
        const float3 ndc = clipPosition.xyz / clipPosition.w;
        float2 depthGradient = 0.0f.xx;
        const bool gradientValid = HikariResolveLocalShadowPlaneDepthGradient(
            shadowViewProj, clipPosition, normalWS, depthGradient);

        const float3 referenceAxis = abs(normalWS.z) < 0.999f
            ? float3(0.0f, 0.0f, 1.0f)
            : float3(0.0f, 1.0f, 0.0f);
        const float3 tangentWS = normalize(cross(referenceAxis, normalWS));
        const float3 bitangentWS = cross(normalWS, tangentWS);
        const float2 centerFaceUv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
        const float epsilon = 0.01f;
        const float4 tangentClip = mul(shadowViewProj, float4(worldPosition + tangentWS * epsilon, 1.0f));
        const float4 bitangentClip = mul(shadowViewProj, float4(worldPosition + bitangentWS * epsilon, 1.0f));
        const float3 tangentNdc = tangentClip.xyz / tangentClip.w;
        const float3 bitangentNdc = bitangentClip.xyz / bitangentClip.w;
        const float2 tangentFaceUv = tangentNdc.xy * float2(0.5f, -0.5f) + 0.5f;
        const float2 bitangentFaceUv = bitangentNdc.xy * float2(0.5f, -0.5f) + 0.5f;
        const float tangentDepthError = tangentNdc.z - ndc.z - dot(depthGradient, tangentFaceUv - centerFaceUv);
        const float bitangentDepthError =
            bitangentNdc.z - ndc.z - dot(depthGradient, bitangentFaceUv - centerFaceUv);
        results[1303u] = float4(depthGradient, tangentDepthError, bitangentDepthError);

        const uint grid = 4u;
        const uint tileResolution = 64u;
        const float atlasSize = float(grid * tileResolution);
        const float2 tile = float2(2.0f, 1.0f);
        const float2 centerAtlasUv = (tile + centerFaceUv) / float(grid);
        const float2 minimumUv = tile / float(grid) + 0.5f / atlasSize;
        const float2 maximumUv = (tile + 1.0f) / float(grid) - 0.5f / atlasSize;
        const float2 sampleUv = clamp(
            centerAtlasUv + float2(1.0f, -1.0f) / atlasSize, minimumUv, maximumUv);
        const float2 sampleFaceUvDelta = (sampleUv - centerAtlasUv) * float(grid);
        const float2 phase = frac(sampleUv * atlasSize - 0.5f);
        const float footprintDepthBias =
            (dot(max(depthGradient, 0.0f.xx), phase) +
             dot(max(-depthGradient, 0.0f.xx), 1.0f.xx - phase)) / float(tileResolution);
        const float tapDepth = HikariLocalShadowTapReferenceDepth(
            ndc.z, depthGradient, sampleFaceUvDelta, sampleUv, atlasSize, tileResolution);
        results[1304u] = float4(phase, sampleFaceUvDelta * float(tileResolution));
        results[1305u] = float4(tapDepth, ndc.z, footprintDepthBias, gradientValid ? 1.0f : 0.0f);

        const float2 edgeFaceUv = float2(0.004f, 0.996f);
        const float2 edgeCenterAtlasUv = (tile + edgeFaceUv) / float(grid);
        const float2 edgeSampleUv = clamp(
            edgeCenterAtlasUv + float2(-1.0f, 1.0f) / atlasSize, minimumUv, maximumUv);
        const float2 edgeFaceUvDelta = (edgeSampleUv - edgeCenterAtlasUv) * float(grid);
        const float2 edgePhase = frac(edgeSampleUv * atlasSize - 0.5f);
        const float edgeFootprintDepthBias =
            (dot(max(depthGradient, 0.0f.xx), edgePhase) +
             dot(max(-depthGradient, 0.0f.xx), 1.0f.xx - edgePhase)) / float(tileResolution);
        const float edgeTapDepth = HikariLocalShadowTapReferenceDepth(
            ndc.z, depthGradient, edgeFaceUvDelta, edgeSampleUv, atlasSize, tileResolution);
        const float3 grazingNormal = normalize(float3(6.0f, 0.0f, 0.7f));
        float2 grazingGradient = 0.0f.xx;
        const bool grazingRejected = !HikariResolveLocalShadowPlaneDepthGradient(
            shadowViewProj, clipPosition, grazingNormal, grazingGradient);
        results[1306u] = float4(edgeFaceUvDelta * float(tileResolution), edgePhase);
        results[1307u] = float4(edgeTapDepth, ndc.z, edgeFootprintDepthBias, grazingRejected ? 1.0f : 0.0f);
    }
    if (all(id == 0u))
    {
        HikariLocalFogVolume box = (HikariLocalFogVolume)0;
        box.worldToBox = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
        box.colorDensity = float4(1,1,1,0.4f);
        box.noiseEdgeScattering = float4(1,0,0.2f,1);
        results[1314u] = float4(HikariLocalFogExtinction(box, 0.0f.xxx),
            HikariLocalFogExtinction(box, float3(.45f,0,0)),
            HikariLocalFogExtinction(box, float3(.5f,0,0)),
            HikariLocalFogExtinction(box, float3(.6f,0,0)));
        box.noiseEdgeScattering.y = 1.0f;
        const float density = HikariLocalFogExtinction(box, float3(.1f,.2f,.3f));
        box.noiseOffset.x = .15f;
        results[1315u] = float4(density, HikariLocalFogExtinction(box, float3(.1f,.2f,.3f)),
            exp(-(.4f + .6f) * 2.0f), HikariExtinctionIntegral(1.0f.xxx, 2.0f).x);
    }
    const float noV = float(id.x) / 20.0f;
    const float2 brdf = HikariAnalyticEnvBrdf(noV, roughness);
    results[id.y * 21u + id.x] = float4(brdf, 0.04f * brdf.x + brdf.y, brdf.x + brdf.y);
    const float slope = float(id.x) / 20.0f;
    const float3 normalDx = float3(slope, -0.5f * slope, 0.25f * slope);
    const float3 normalDy = float3(0.3f * slope, slope, -0.1f * slope);
    // 同じ微分を正規直交基底で回転してもフィルタ幅は変わらない。
    const float c = sqrt(0.5f);
    const float3 rotatedDx = float3(c * (normalDx.x - normalDx.y),
        c * (normalDx.x + normalDx.y), normalDx.z);
    const float3 rotatedDy = float3(c * (normalDy.x - normalDy.y),
        c * (normalDy.x + normalDy.y), normalDy.z);
    results[487u + id.y * 21u + id.x] = float4(
        HikariFilterPerceptualRoughness(roughness, normalDx, normalDy),
        HikariFilterPerceptualRoughness(roughness, rotatedDx, rotatedDy),
        HikariFilterPerceptualRoughness(roughness, normalDy, -normalDx),
        HikariFilterPerceptualRoughness(roughness, 0.0f.xxx, 0.0f.xxx));
    if (id.y == 0u)
    {
        HikariPbrShadingContext context = (HikariPbrShadingContext)0;
        context.normal = float3(0.0f, 0.0f, 1.0f);
        const float perceptualRoughness = max(0.04f, float(id.x) / 20.0f);
        const float alpha = perceptualRoughness * perceptualRoughness;
        context.distributionAlphaSquared = alpha * alpha;
        results[441u + id.x] = float4(
            HikariDistributionGGX(context, context.normal),
            HikariDistributionGGX(context, float3(sqrt(0.75f), 0.0f, 0.5f)),
            perceptualRoughness, 0.0f);
    }
    if (id.y == 1u && id.x < 4u)
    {
        float authoredRoughness = 0.2f;
        float authoredFactor = 0.8f;
        float3 authoredColor = float3(0.9f, 0.6f, 0.3f);
        HikariApplyLegacySpecularResponse(id.x >= 2u, (id.x & 1u) != 0u,
            authoredRoughness, authoredColor, authoredFactor);
        results[462u + id.x] = float4(authoredRoughness, authoredFactor, authoredColor.x, authoredColor.z);
    }
    if (id.y == 2u)
    {
        HikariLocalLightGpuData light = (HikariLocalLightGpuData)0;
        light.positionRange = float4(0, 0, 10, 20);
        light.colorIntensity = float4(0.5f, 0.25f, 1.0f, 2.0f);
        HikariLocalLightSample sample;
        HikariResolveLocalLightSample(light, float3(0, 0, float(id.x)), sample);
        results[466u + id.x] = float4(sample.radiance,
            HikariLocalLightConeAttenuation(float(id.x) / 20.0f, 0.8f, 0.6f, 3.0f));
    }
    if (id.y == 3u)
    {
        const float sx = (0.25f + 0.1f * id.x) * ((id.x & 1u) != 0u ? -1.0f : 1.0f);
        const float sy = 1.75f;
        const float sz = 0.7f;
        const float3x3 world = float3x3(sx, 0.25f, 0, 0, sy, 0, 0, 0, sz);
        const float sourceSign = id.x % 3u == 0u ? -1.0f : 1.0f;
        const float3 worldNormal = normalize(float3(1.0f / sx, (1.0f - 0.25f / sx) / sy, 1.0f / sz));
        const float4 tangent = HikariTransformSurfaceTangent(float4(1, -1, 0, sourceSign), world);
        results[928u + id.x * 4u] = tangent;
        results[929u + id.x * 4u] = float4(HikariTransformTangentNormalToWorld(
            float3(0.3f, 0.4f, sqrt(0.75f)), worldNormal, tangent), 1.0f);
        const float nanValue = asfloat(0x7fc00000u);
        results[930u + id.x * 4u] = float4(HikariTransformTangentNormalToWorld(
            float3(0.3f, 0.4f, sqrt(0.75f)), worldNormal,
            HikariTransformSurfaceTangent(float4((id.x & 1u) != 0u ? nanValue : 0.0f, 0, 0, 1), world)), 1.0f);
        const float3 invalidNormal = id.x % 3u == 0u ? float3(nanValue, 0, 1)
            : id.x % 3u == 1u ? float3(0.3f, 0.4f, sqrt(0.75f)) : 0.0f.xxx;
        const float4 degenerateTangent = id.x % 3u == 1u ? float4(worldNormal, 1.0f) : tangent;
        results[931u + id.x * 4u] = float4(HikariTransformTangentNormalToWorld(
            invalidNormal, worldNormal, degenerateTangent), 1.0f);
    }
    if (id.y == 4u)
    {
        HikariLocalLightGpuData light = (HikariLocalLightGpuData)0;
        light.positionRange = float4(0, 0, 0, 10);
        light.colorIntensity = float4(0.5f, 0.25f, 1.0f, 2.0f);
        light.kind = HIKARI_LOCAL_LIGHT_TYPE_POINT;
        HikariLocalLightSample sample;
        HikariResolveLocalLightSample(light, float3(0, 0, float(id.x)), sample);
        results[1012u + id.x * 3u] = float4(sample.radiance, sample.attenuation);
        light.outerConeCos = 0.6f;
        light.angularFalloff = 3;
        light.kind |= HIKARI_LOCAL_LIGHT_TYPE_SPOT;
        light.directionInnerCos = float4(0, 0, 1, 0.8f);
        HikariResolveLocalLightSample(light, float3(0, 0, float(id.x)), sample);
        results[1013u + id.x * 3u] = float4(sample.radiance, sample.attenuation);
        // 十分遠いカットオフ、ゼロ範囲、発光体半径での頭打ち。
        results[1014u + id.x * 3u] = float4(
            HikariLocalLightRangeAttenuation(float(id.x), 100),
            HikariLocalLightRangeAttenuation(float(id.x), 200),
            HikariLocalLightRangeAttenuation(float(id.x), 0),
            HikariLocalLightRangeAttenuation(float(id.x), 10, 0.5f));
    }
    if (id.y == 5u)
    {
        const float elevation = (float(id.x) - 10.0f) / 10.0f;
        const float3 direction = float3(sqrt(saturate(1.0f - elevation * elevation)), elevation, 0);
        results[1076u + id.x * 4u] = float4(HikariApplySkyRadianceScale(
            float3(0.4f,0.8f,1.2f), float3(0.5f,2,0.25f), float(id.x) * 0.25f), 1);
        results[1077u + id.x * 4u] = float4(HikariRotateSkyYaw(direction, 1.570796326794897f), 0.0f);
    }
    if (id.y == 7u)
    {
        const float distance = float(id.x) * 0.1f;
        results[1180u + id.x] = float4(
            HikariLocalLightSourceHalfAngleSineSquared(distance, 0.5f),
            HikariLocalLightRangeAttenuation(distance, 10, 0.5f),
            HikariLocalLightRangeAttenuation(distance, 20, 0.5f),
            HikariLocalLightSourceHalfAngleSineSquared(distance, 0.0f));
    }

}

// 各 lane が一つの状況を同じ問い合わせで検査する。sampler の巨大な重複展開を避ける。
[numthreads(1,1,1)]
void DynamicGiMain(uint3 id : SV_DispatchThreadID)
{
    const uint test = id.x;
    if (test >= 101u) return;
    const uint outputIndex = 1316u + test;
    if (test == 76u)
    {
        const float4 disk = float4(0,.5f,-.1f,.05f);
        results[outputIndex] = float4(
            HikariDynamicGiPatchWeight(disk,float3(1,0,0),float3(.11f,0,0),.002f),
            HikariDynamicGiPatchWeight(disk,float3(1,0,0),float3(.11f,.1f,0),.002f),
            HikariDynamicGiPatchWeight(disk,float3(1,0,0),float3(.1f,0,0),.002f),
            HikariDynamicGiPatchWeight(0.0f.xxxx,float3(1,0,0),float3(.11f,0,0),.002f));
        return;
    }
    if (test == 77u)
    {
        const float4 disk = float4(0,.5f,-.001f,.2f);
        results[outputIndex] = float4(
            HikariDynamicGiPatchWeight(disk,float3(1,0,0),float3(.0011f,.1f,0),.002f),
            HikariDynamicGiPatchWeight(disk,float3(1,0,0),float3(.01f,.1f,0),.002f),
            HikariDynamicGiWeight(0.0f,1.0f),HikariDynamicGiWeight(.00001f,1.0f));
        return;
    }
    HikariIrradianceVolumeData volumes[HIKARI_DYNAMIC_GI_VOLUME_COUNT];
    [unroll] for (uint v=0u;v<HIKARI_DYNAMIC_GI_VOLUME_COUNT;++v) volumes[v]=(HikariIrradianceVolumeData)0;
    HikariIrradianceVolumeData volume;
    volume.originSampling=float4(0,0,0,3);
    volume.spacingIntensity=float4(1,1,1,1);
    volume.countsProbeCount=float4(4,2,2,16);
    volume.sizeBlendDistance=float4(3,1,1,.1f);
    volume.selectionParams=float4(0,1,.25f,10);
    HikariDynamicGiQuery query=(HikariDynamicGiQuery)0;
    query.medium=true;
    query.position=float3(.8f,.5f,.5f);
    bool combine=false;
    if (test < 63u || (test >= 79u && test < 100u))
    {
        const uint lane = test < 63u ? test % 21u : test - 79u;
        query.position.x=.8f+float(lane)*.02f;
        if ((test >= 21u && test < 42u) || test >= 79u)
            query=HikariSurfaceGiQuery(query.position,float3(0,1,0),test>=79u?float3(1,0,0):float3(0,1,0));
        if (test >= 42u && test < 63u)
        {
            volumes[1]=volume;
            volume.spacingIntensity.w=4;
            combine=true;
        }
    }
    else if (test == 63u) { volume.spacingIntensity.w=0; combine=true; }
    else if (test == 64u) query.position.x=3;
    else if (test == 65u) { query.position.x=-1; combine=true; }
    else if (test == 66u) { query.position.x=1; combine=true; }
    else if (test == 67u) { query.position.x=.05f; combine=true; }
    else if (test == 68u || test == 69u)
        query=HikariSurfaceGiQuery(float3(test==68u?1.8f:2.8f,.5f,.5f),float3(1,0,0),float3(0,1,0));
    else if (test >= 70u && test <= 72u)
    {
        // 変種 1：probe を格子点から +x へ 0.25 動かした状態（再配置）。
        if (test != 70u) volume.selectionParams.y=2;
        if (test == 72u) query.position.x=3;
        else query=HikariSurfaceGiQuery(float3(.3f,.5f,.5f),float3(1,0,0),float3(0,1,0));
    }
    else if (test >= 73u && test <= 75u)
    {
        // 変種 2：全 probe の +x 側 0.5 m に円盤。
        volume.selectionParams.y=3;
        query=HikariSurfaceGiQuery(float3(test==73u?1.6f:test==74u?1.0f:1.4f,0,0),float3(-1,0,0),float3(0,1,0));
    }
    else if (test == 78u || test == 100u)
    {
        // 影響の箱は原点から size まで。境界上の点は影響 0 なので、箱の内側へ寄せる（y と z は補間に効かない）。
        volume.sizeBlendDistance=float4(3,1.2f,1.2f,.1f);
        volumes[1]=volume;
        volume.selectionParams.y=3;
        query=HikariSurfaceGiQuery(float3(1.6f,.1f,.1f),float3(-1,0,0),float3(0,1,0));
        combine=test==78u;
    }
    volumes[0]=volume;
    HikariDynamicGiSample sample;
    [branch] if (combine) sample=HikariQueryDynamicGi(volumes,query);
    else sample=HikariEvaluateDynamicGiVolume(volume,query);
    results[outputIndex] = test < 21u ? float4(sample.value.x,sample.coverage,sample.support,sample.skyVisibility)
        : ((test>=21u && test<42u) || test>=79u) ? float4(sample.value.x,sample.coverage,0,0)
        : float4(sample.value,sample.coverage);
}
