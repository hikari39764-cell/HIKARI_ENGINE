#include "Include/Lighting/HIKARI_RuntimeRayLighting.hlsli"
#include "Include/Lighting/HIKARI_IrradianceSh9.hlsli"
#include "Include/Surface/HIKARI_SceneSurfaceContract.hlsli"
#include "Include/Surface/HIKARI_ScreenSpaceGeometry.hlsli"
#include "Include/Color/HIKARI_ViewExposure.hlsli"

// 光線追跡の反射。光線追跡が使える時は SSR の trace の代わりに、反射を受ける全 texel から実行時の加速構造へ反射光線を
// 一本ずつ飛ばす（SSR と光線追跡で向きと当たる形状が食い違わず、境目が出ない）。当たった点が画面に見えていればその画素の
// 照明済みの色（GI・局所光の影・AO を含む）を使い、見えていなければ太陽・局所光・動的 GI で照らす。空へ抜けた光線は
// 確信度 0 を書いて既存の環境 fallback に任せる。出力は SSR と同じ「Pre-Exposure 済みの放射輝度 + 確信度」で、後段の
// 履歴（空間と時間の降噪）と合成はそのまま使う。粗さは GGX の見える法線で標本化し、画素とフレームで乱数を変える。

cbuffer RayTracedReflectionCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPosition;
    float4 gScreenParams;      // 深度の大きさ（xy）と 1/大きさ（zw）
    float4 gTraceScreenParams; // trace の大きさ（xy）と 1/大きさ（zw）
    float4 gRayParams;         // x 最大距離, y 最大粗さ, z フレーム番号, w 未使用
    float4 gViewExposure;
    HikariIrradianceVolumeData gGiVolumes[HIKARI_DYNAMIC_GI_VOLUME_COUNT];
    HikariRayLightingData gRayLighting;
};

Texture2D<float> gSceneDepth : register(t0, space9);
Texture2D<float4> gSceneSurface : register(t1, space9);
Texture2D<float4> gSceneColor : register(t2, space9);
RWTexture2D<float4> gTrace : register(u0, space9);
SamplerState gLinearClamp : register(s0);
SamplerState gMaterialWrap : register(s1);
SamplerState gPointClamp : register(s2);

// 動的 GI の probe atlas だけを読む（焼き込みの SH 体積は宣言だけで使わない）。
Texture3D<float4> gLightProbeShVolume0[9] : register(t0, space2);
#define gLightProbeShVolume1 gLightProbeShVolume0
#define gLinearWrap gLinearClamp
#define HIKARI_IRRADIANCE_SAMPLE_ONLY 1
#include "Include/Lighting/HIKARI_LightingProviderSampling.hlsli"

// これより滑らかな面は鏡の向きだけを使う（乱数の揺れが無い）。
static const float kMirrorRoughness = 0.05f;

uint ReflectionHash(uint value)
{
    value = value * 747796405u + 2891336453u;
    value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    return (value >> 22u) ^ value;
}

// 画素ごとの一様な乱数回転に R2 列（フレームごと）を足した 2 次元の乱数。画面に規則的な模様（斜めの縞）を作らず、
// 一つの画素では時間方向に低食い違いで並ぶ。
float2 ReflectionRandom(uint2 pixel, uint frame, uint stream)
{
    const uint hash = ReflectionHash(pixel.x * 73856093u ^ pixel.y * 19349663u ^ stream * 83492791u);
    const float2 rotation = float2(hash & 0xffffu, hash >> 16u) / 65536.0f;
    return frac(rotation + float2(0.7548776662f, 0.5698402910f) * float(frame));
}

// GGX の見える法線を球冠で標本化し（Dupuy & Benyoub 2023）、その法線での反射の向きを返す。
float3 SampleGgxReflection(float3 normal, float3 view, float roughness, float2 xi)
{
    const float alpha = max(roughness * roughness, 1.0e-3f);
    const float3 up = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    const float3 localView = float3(dot(view, tangent), dot(view, bitangent), dot(view, normal));
    const float3 stretched = normalize(float3(localView.xy * alpha, localView.z));
    const float phi = 6.28318530718f * xi.x;
    const float z = (1.0f - xi.y) * (1.0f + stretched.z) - stretched.z;
    const float sinTheta = sqrt(saturate(1.0f - z * z));
    const float3 cap = float3(sinTheta * cos(phi), sinTheta * sin(phi), z) + stretched;
    const float3 localHalf = normalize(float3(cap.xy * alpha, max(cap.z, 0.0f)));
    return reflect(-view, tangent * localHalf.x + bitangent * localHalf.y + normal * localHalf.z);
}

// 当たった点が画面に見えていれば（深度が一致し、面の向きも近い）、その画素の照明済みの色。
bool ScreenHitRadiance(float3 position, float3 normal, out float3 radiance)
{
    radiance = 0.0f.xxx;
    float2 uv;
    float projectedDepth;
    float viewDepth;
    if (!HikariProjectWorldPosition(position, gViewProj, uv, projectedDepth, viewDepth) || any(uv <= 0.0f) ||
        any(uv >= 1.0f))
        return false;
    const float depth = gSceneDepth.SampleLevel(gPointClamp, uv, 0.0f);
    float sceneViewDepth;
    if (!HikariIsValidSceneDepth(depth) || !HikariReconstructViewDepth(uv, depth, gInvViewProj, sceneViewDepth) ||
        abs(sceneViewDepth - viewDepth) > max(viewDepth * 0.01f, 0.02f))
        return false;
    float3 sceneNormal;
    float roughness;
    float metallic;
    HikariUnpackSceneSurface(gSceneSurface.SampleLevel(gPointClamp, uv, 0.0f), sceneNormal, roughness, metallic);
    if (dot(sceneNormal, normal) < 0.5f)
        return false;
    radiance = gSceneColor.SampleLevel(gPointClamp, uv, 0.0f).rgb;
    return all(isfinite(radiance)) && all(radiance >= 0.0f);
}

// 画面外の当たった点へ向き direction のまわりから届く光（E/pi）。動的 GI（手置き区域、近景、遠景の順）が覆う分は probe
// から読み、覆わない分だけその向きの空（skyMip でぼかす）で近似する。閉じた部屋の中を空の光で照らさない。
// 拡散は面の法線、鏡面（金属の下面が床を映すなど）は反射の向きで読む。
float3 HitIncidentLight(HikariRaySurface hit, float3 direction, float3 view, float skyMip)
{
    float3 diffuse = 0.0f.xxx;
    float coverage = 0.0f;
    [loop] for (uint index = 0u; index < HIKARI_DYNAMIC_GI_VOLUME_COUNT; ++index)
    {
        const HikariIrradianceVolumeData volume = gGiVolumes[index];
        const float influence = HikariEvaluateIrradianceVolumeInfluence(volume, hit.position);
        [branch] if (influence > 0.0001f)
        {
            float sky;
            const float4 sample =
                HikariEvaluateDynamicGiVolume(volume, hit.position, direction, view, int3(-1, -1, -1), sky);
            const float weight = influence * sample.a * (1.0f - coverage);
            diffuse += sample.rgb * max(0.0f, volume.spacingIntensity.w) * weight;
            coverage += weight;
            [branch] if (coverage >= 0.9999f)
                return diffuse;
        }
    }
    return diffuse + HikariRayEnvironmentRadiance(gRayLighting, direction, gLinearClamp, skyMip) * (1.0f - coverage);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint2 pixel = id.xy;
    if (any(pixel >= uint2(gTraceScreenParams.xy)))
        return;
    // 全 texel を書く（SSR の trace はこのフレーム走っていない）。反射が無い texel は確信度 0。
    gTrace[pixel] = 0.0f.xxxx;
    const float2 uv = (float2(pixel) + 0.5f) * gTraceScreenParams.zw;
    const float depth = gSceneDepth.SampleLevel(gPointClamp, uv, 0.0f);
    if (!HikariIsValidSceneDepth(depth))
        return;
    float3 normal;
    float roughness;
    float metallic;
    HikariUnpackSceneSurface(gSceneSurface.SampleLevel(gPointClamp, uv, 0.0f), normal, roughness, metallic);
    if (!(roughness < gRayParams.y))
        return;
    float3 position;
    if (!HikariReconstructWorldPosition(uv, depth, gInvViewProj, position))
        return;
    const float3 view = normalize(gCameraPosition.xyz - position);
    if (dot(view, normal) <= 0.0f)
        return;

    const uint frame = uint(gRayParams.z);
    float3 direction = roughness < kMirrorRoughness
        ? reflect(-view, normal)
        : SampleGgxReflection(normal, view, roughness, ReflectionRandom(pixel, frame, 0u));
    if (dot(direction, normal) <= 0.01f)
        direction = reflect(-view, normal);

    RayDesc ray;
    ray.Origin = position + normal * 0.01f;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = max(gRayParams.x, 0.1f);
    HikariRaySurface hit;
    if (!HikariTraceRuntimeScene(ray, gRayLighting.scene.x, gRayLighting.scene.y, gRayLighting.time, gMaterialWrap,
            HIKARI_RAY_REPORT_BACKFACES, hit))
        return;
    // 片面の物の裏に当たったのは始点が物の中にある時（接地した球の根元では法線方向のずらしで床の点が球の中へ入る）。
    // 裏を飛ばすと光線が物を抜けて空を映し、接地線が白く光るので、遮られた（暗い）ものとして数える。
    if (hit.backFace != 0u &&
        (HikariGetMeshMaterialData(hit.materialIndex).instanceFlags & HIKARI_PROGRAM_FLAG_DOUBLE_SIDED) == 0u)
    {
        gTrace[pixel] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    float3 radiance;
    if (!ScreenHitRadiance(hit.position, hit.normal, radiance))
    {
        const float3 hitView = -direction;
        radiance = HikariPreExpose(HikariRayHitRadiance(gRayLighting, hit, hitView, ReflectionRandom(pixel, frame, 1u),
            HitIncidentLight(hit, hit.normal, hitView, 4.0f),
            HitIncidentLight(hit, reflect(direction, hit.normal), hitView, hit.pbr.roughness * 6.0f), true,
            gMaterialWrap), gViewExposure);
    }
    gTrace[pixel] = float4(radiance, 1.0f);
}
