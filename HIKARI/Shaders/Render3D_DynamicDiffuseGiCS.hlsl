#include "Include/Lighting/HIKARI_RuntimeRayLighting.hlsli"
#include "Include/Lighting/HIKARI_IrradianceSh9.hlsli"

Texture3D<float4> gLightProbeShVolume0[9] : register(t0, space2);
#define gLightProbeShVolume1 gLightProbeShVolume0
SamplerState gLinearClamp : register(s0);
SamplerState gMaterialWrap : register(s1);
#define gLinearWrap gLinearClamp
#define HIKARI_IRRADIANCE_SAMPLE_ONLY 1
#include "Include/Lighting/HIKARI_LightingProviderSampling.hlsli"
RWTexture3D<float4> gOutputSh[9] : register(u0);
RWTexture2D<float2> gOutputVisibility : register(u9);

cbuffer DynamicDiffuseGiCB : register(b0)
{
    HikariIrradianceVolumeData gPrevious;
    float4 gOriginSpacing;
    uint4 gCounts; // xyz counts, w total
    uint4 gWork; // sequence, first probe, budget, history compatible
    HikariRayLightingData gRayLighting;
};

groupshared float3 sDirection[64];
groupshared float3 sRadiance[64];
groupshared float sDistance[64];
groupshared uint sBackFace[64];
groupshared float3 sRelocation;
groupshared float sValidity;

float3 ProbeRayDirection(uint rayIndex, uint sequence)
{
    const uint count = rayIndex < 16u ? 16u : 48u;
    const uint index = rayIndex < 16u ? rayIndex : rayIndex-16u;
    const float y = 1.0f - 2.0f * (float(index)+0.5f)/float(count);
    const float angle = 2.39996322973f*float(index) + (rayIndex < 16u ? 0.0f : float(sequence)*0.754877666f);
    const float radius = sqrt(saturate(1.0f-y*y));
    return float3(cos(angle)*radius,y,sin(angle)*radius);
}

float3 HitRadiance(HikariRaySurface hit, float3 view, float2 randomUv)
{
    const float4 previous = HikariEvaluateLightProbeVolumeDiffuse(0u,gPrevious,hit.position,hit.normal);
    // 未初期化 probe へ空を注入すると室内へ毎フレーム漏れる。有効な拡散履歴だけを渡す。
    return HikariRayHitRadiance(gRayLighting,hit,view,randomUv,previous.rgb*previous.a*
        HikariEvaluateIrradianceVolumeInfluence(gPrevious,hit.position),false,gMaterialWrap);
}

[numthreads(64,1,1)]
void main(uint3 groupId : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint probeIndex = groupId.x;
    if (probeIndex >= gCounts.w) return;
    const uint3 coordinate = uint3(probeIndex%gCounts.x,(probeIndex/gCounts.x)%gCounts.y,probeIndex/(gCounts.x*gCounts.y));
    const float3 nominalPosition = gOriginSpacing.xyz+float3(coordinate)*gOriginSpacing.w;
    const int3 previousCoordinate = int3(round((nominalPosition-gPrevious.originSampling.xyz)/gOriginSpacing.w));
    const bool hasPrevious = gWork.w != 0u && all(previousCoordinate>=0) && all(previousCoordinate<int3(gCounts.xyz));
    float4 previousSh[9];
    [unroll] for (uint c=0u;c<9u;++c) previousSh[c] = hasPrevious ? gLightProbeShVolume0[c].Load(int4(previousCoordinate,0)) : 0.0f.xxxx;
    const uint2 tile = uint2(coordinate.x+coordinate.y*gCounts.x,coordinate.z)*10u;
    const uint2 previousTile = uint2(previousCoordinate.x+previousCoordinate.y*gCounts.x,previousCoordinate.z)*10u;
    const uint previousVisibilityIndex = uint(gPrevious.selectionParams.y+0.5f)-1u;
    // 物理格納順と更新順を分離し、再初期化時はカメラ付近から有効にする。
    const int3 centerDelta = int3(coordinate)-int3(gCounts.xyz/2u);
    const uint3 centerRank = min(uint3(abs(centerDelta)*2-int3(centerDelta>0)),gCounts.xyz-1u);
    const uint updateIndex = centerRank.x+centerRank.y*gCounts.x+centerRank.z*gCounts.x*gCounts.y;
    const uint selected = (updateIndex+gCounts.w-gWork.y)%gCounts.w;
    if (selected >= gWork.z)
    {
        if (lane < 9u) gOutputSh[lane][coordinate] = previousSh[lane];
        for (uint texel=lane;texel<100u;texel+=64u)
            gOutputVisibility[tile+uint2(texel%10u,texel/10u)] = hasPrevious
                ? gMaterialTexturePool[NonUniformResourceIndex(previousVisibilityIndex)].Load(int3(previousTile+uint2(texel%10u,texel/10u),0)).rg : 0.0f.xx;
        return;
    }
    const float3 oldRelocation = float3(previousSh[1].a,previousSh[2].a,previousSh[3].a);
    const float3 position = nominalPosition+oldRelocation*gOriginSpacing.w;
    const float maximumDistance = gPrevious.selectionParams.w;
    const float3 direction = ProbeRayDirection(lane,gWork.x);
    // Visibility atlas の正規化距離は ray の打切り距離ではない。
    // 大空間の壁/屋根を打ち切って miss=sky と扱うと室内へ漏光する。
    RayDesc ray; ray.Origin=position; ray.Direction=direction; ray.TMin=0.001f; ray.TMax=1e7f;
    HikariRaySurface hit = (HikariRaySurface)0;
    bool found = false;
    // 48 本の放射サンプルが球面を分担する円錐。分類 ray は点で形状を検査する。
    const float coneCosine = 1.0f - 2.0f / 48.0f;
    const float coneSpread = lane < 16u ? 0.0f : 2.0f * sqrt(1.0f-coneCosine*coneCosine) / coneCosine;
    if (gRayLighting.scene.w == 0u) found = HikariTraceRuntimeScene(ray,gRayLighting.scene.x,gRayLighting.scene.y,gRayLighting.time,gMaterialWrap,
        HIKARI_RAY_REPORT_BACKFACES,hit,float2(0.0f,coneSpread));
    sDirection[lane] = direction;
    sDistance[lane] = found ? min(hit.distance,maximumDistance) : maximumDistance;
    sBackFace[lane] = found ? hit.backFace : 0u;
    const float2 randomUv = frac(float2(lane+1u,gWork.x+probeIndex+1u)*float2(0.754877666f,0.569840296f)+float2(gWork.x,lane)*0.618033989f);
    sRadiance[lane] = lane < 16u || (found && hit.backFace != 0u) ? 0.0f.xxx
        : found ? HitRadiance(hit,-direction,randomUv) : HikariRayEnvironmentRadiance(gRayLighting,direction,gLinearClamp);
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0u)
    {
        uint backFaces=0u; float nearestBack=maximumDistance; float nearestFront=maximumDistance;
        float3 backDirection=0.0f.xxx,frontDirection=0.0f.xxx;
        for (uint i=0u;i<16u;++i)
        {
            if (sBackFace[i]!=0u) { ++backFaces; if (sDistance[i]<nearestBack) { nearestBack=sDistance[i]; backDirection=sDirection[i]; } }
            else if(sDistance[i]<nearestFront) { nearestFront=sDistance[i]; frontDirection=sDirection[i]; }
        }
        float3 offset=oldRelocation;
        const float clearance=gOriginSpacing.w*0.1f;
        if (backFaces>4u) offset += backDirection*(nearestBack+clearance)/gOriginSpacing.w;
        else if(nearestFront<clearance) offset -= frontDirection*(clearance-nearestFront)/gOriginSpacing.w;
        if (length(offset)>0.45f) offset=oldRelocation;
        sRelocation=offset;
        sValidity=backFaces<=4u && length(offset-oldRelocation)<0.001f ? 1.0f : 0.0f;
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane<9u)
    {
        float3 coefficient=0.0f.xxx;
        for(uint i=16u;i<64u;++i) coefficient+=sRadiance[i]*HikariSh9Basis(sDirection[i],lane)*(12.56637061436f/48.0f);
        float3 average=0.0f.xxx;
        for(uint j=16u;j<64u;++j) average+=sRadiance[j]/48.0f;
        const float3 oldAverage=previousSh[0].rgb*0.2820947918f;
        const float change=length(average-oldAverage)/max(0.01f,max(length(average),length(oldAverage)));
        const float history=previousSh[0].a>0.0f && sValidity>0.0f ? lerp(0.9f,0.0f,saturate(change*2.0f)) : 0.0f;
        const float alpha=lane==0u ? sValidity : lane<=3u ? sRelocation[lane-1u] : 0.0f;
        gOutputSh[lane][coordinate]=float4(lerp(coefficient,previousSh[lane].rgb,history),alpha);
    }
    for(uint texel=lane;texel<100u;texel+=64u)
    {
        const uint2 pixel=uint2(texel%10u,texel/10u);
        const float3 axis=HikariDecodeOctahedralDirection((float2(HikariWrapOctahedralTexel(int2(pixel)-1,8))+0.5f)/8.0f);
        float weightSum=0.0f; float2 moments=0.0f.xx;
        for(uint i=0u;i<64u;++i)
        {
            const float weight=pow(saturate(dot(axis,sDirection[i])),16.0f);
            const float distance=sDistance[i]/maximumDistance;
            moments+=float2(distance,distance*distance)*weight; weightSum+=weight;
        }
        // 可視性は radiance history と別に即時更新し、動く遮蔽物の古い影を引きずらない。
        gOutputVisibility[tile+pixel]=moments/max(weightSum,1e-8f);
    }
}
