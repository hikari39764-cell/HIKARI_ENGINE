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
RWTexture2D<float4> gOutputAtlas : register(u9);
// 近景の置き場所を決める相機の ray（中央・右・左・上・下・真下）が当たった点の高さ（当たらなければ 1e30）と、
// その後ろ 6 個に相機からの水平距離。
RWStructuredBuffer<float> gFocusHeights : register(u10);

cbuffer DynamicDiffuseGiCB : register(b0)
{
    HikariIrradianceVolumeData gPrevious;
    float4 gOrigin; // xyz 格子の原点
    float4 gSpacing; // xyz 軸ごとの間隔
    uint4 gCounts; // xyz counts, w total
    uint4 gWork; // sequence, first probe, budget, history compatible
    HikariRayLightingData gRayLighting;
    float4 gFocusOrigin;    // xyz 相機の位置、w は近景の更新で 1（相機の ray を撃つ）
    float4 gFocusDirection; // xyz 視線の向き
};

// probe 一回の更新の ray。先頭 16 本は形状の内外を調べる固定の分類 ray、残りは更新ごとに回す放射サンプル。
// 部屋の開口のような小さく明るい光源を拾う数が揺れるので、64 本ではちらつく。
static const uint kRayCount = 128u, kClassifyRays = 16u, kRadianceRays = kRayCount - kClassifyRays;
groupshared float3 sDirection[kRayCount];
groupshared float3 sRadiance[kRayCount];
groupshared float sDistance[kRayCount];
groupshared uint sBackFace[kRayCount];
groupshared float3 sSh[9];
groupshared float sSky[kRayCount];
groupshared float sSkySh[4];
groupshared float sValidity;
// 隣の 26 probe（3x3x3 の添字、中央 13 は自分）との間を形状が塞いでいなければ 1。
groupshared uint sNeighborOpen[27];

// 卦限 octant（bit が立つ軸は負の向き）の隣の 7 probe の連通 bit（軸の差の組 x=1, y=2, z=4 から 1 を引いた位置）。
uint ProbeOctantLinks(uint octant)
{
    const int3 sign = int3((octant & 1u) != 0u ? -1 : 1, (octant & 2u) != 0u ? -1 : 1, (octant & 4u) != 0u ? -1 : 1);
    uint links = 0u;
    [unroll] for (uint axes = 1u; axes < 8u; ++axes)
    {
        const int3 offset = int3((axes & 1u) != 0u ? sign.x : 0, (axes & 2u) != 0u ? sign.y : 0,
            (axes & 4u) != 0u ? sign.z : 0) + 1;
        links |= sNeighborOpen[offset.x + offset.y * 3 + offset.z * 9] << (axes - 1u);
    }
    return links;
}

uint ProbeHash(uint value)
{
    value = value * 747796405u + 2891336453u;
    value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    return (value >> 22u) ^ value;
}

// 放射サンプルの球面 Fibonacci を更新ごと・probe ごとに一様な乱数回転で回す（Shoemake の単位四元数）。
// 方位角だけ回すと仰角の帯が毎回同じになり、深度と放射照度の推定に偏りが残る。分類 ray は固定する。
float3x3 ProbeRayRotation(uint sequence, uint probeIndex)
{
    const uint seed = ProbeHash(sequence * 9781u + probeIndex * 6271u + 1u);
    const float u1 = float(ProbeHash(seed) & 0xffffu) / 65535.0f;
    const float u2 = float(ProbeHash(seed + 1u) & 0xffffu) / 65535.0f * 6.28318530718f;
    const float u3 = float(ProbeHash(seed + 2u) & 0xffffu) / 65535.0f * 6.28318530718f;
    const float a = sqrt(1.0f - u1), b = sqrt(u1);
    const float4 q = float4(a * sin(u2), a * cos(u2), b * sin(u3), b * cos(u3));
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    return float3x3(1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (x * z + y * w),
                    2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w),
                    2.0f * (x * z - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y));
}

float3 ProbeRayDirection(uint rayIndex, float3x3 rotation)
{
    const uint count = rayIndex < kClassifyRays ? kClassifyRays : kRadianceRays;
    const uint index = rayIndex < kClassifyRays ? rayIndex : rayIndex-kClassifyRays;
    const float y = 1.0f - 2.0f * (float(index)+0.5f)/float(count);
    const float angle = 2.39996322973f*float(index);
    const float radius = sqrt(saturate(1.0f-y*y));
    const float3 direction = float3(cos(angle)*radius,y,sin(angle)*radius);
    return rayIndex < kClassifyRays ? direction : mul(rotation, direction);
}

// shooter は ray を撃った probe の前回の格子座標。当たった点の補間の基準にする（その間に形状は無い）。
float3 HitRadiance(HikariRaySurface hit, float3 view, float2 randomUv, int3 shooter)
{
    float sky;
    const float4 previous = HikariEvaluateDynamicGiVolume(gPrevious,hit.position,hit.normal,view,shooter,sky);
    // 未初期化 probe へ空を注入すると室内へ毎フレーム漏れる。有効な拡散履歴だけを渡す（coverage は有効な probe の有無）。
    return HikariRayHitRadiance(gRayLighting,hit,view,randomUv,previous.rgb*previous.a*
        HikariEvaluateIrradianceVolumeInfluence(gPrevious,hit.position),0.0f.xxx,false,gMaterialWrap);
}

[numthreads(64,1,1)]
void main(uint3 groupId : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint probeIndex = groupId.x;
    if (probeIndex >= gCounts.w) return;
    // 近景の cascade を相機が見ている面の近くへ下ろすため、近景の更新の最初の group が相機から ray を撃つ
    // （C++ の PlaceNearField が数フレーム遅れて読む）。
    if (probeIndex == 0u && lane < 6u && gFocusOrigin.w > 0.5f)
    {
        const float3 forward = normalize(gFocusDirection.xyz);
        const float3 right = abs(forward.y) < 0.99f ? normalize(cross(float3(0.0f, 1.0f, 0.0f), forward))
            : float3(1.0f, 0.0f, 0.0f);
        const float3 up = cross(forward, right);
        const float3 offsets[5] = {0.0f.xxx, right, -right, up, -up};
        RayDesc focus;
        focus.Origin = gFocusOrigin.xyz;
        focus.Direction = lane == 5u ? float3(0.0f, -1.0f, 0.0f) : normalize(forward + offsets[min(lane, 4u)] * 0.3f);
        focus.TMin = 0.01f;
        focus.TMax = 500.0f;
        HikariRaySurface focusHit = (HikariRaySurface)0;
        const bool found = gRayLighting.scene.w == 0u && HikariTraceRuntimeScene(focus, gRayLighting.scene.x,
            gRayLighting.scene.y, gRayLighting.time, gMaterialWrap, HIKARI_RAY_REPORT_BACKFACES, focusHit);
        gFocusHeights[lane] = found ? focus.Origin.y + focus.Direction.y * focusHit.distance : 1.0e30f;
        gFocusHeights[lane + 6u] = found ? length(focus.Direction.xz) * focusHit.distance : 0.0f;
    }
    const uint3 coordinate = uint3(probeIndex%gCounts.x,(probeIndex/gCounts.x)%gCounts.y,probeIndex/(gCounts.x*gCounts.y));
    const float3 nominalPosition = gOrigin.xyz+float3(coordinate)*gSpacing.xyz;
    // 前の格子は自分の間隔と数で引く（画質の档位を切り替えた直後は前の格子と間隔も数も違い、近い probe を引き継ぐ）。
    const uint3 previousCounts = uint3(gPrevious.countsProbeCount.xyz + 0.5f);
    const int3 previousCoordinate =
        int3(round((nominalPosition-gPrevious.originSampling.xyz)/gPrevious.spacingIntensity.xyz));
    const bool hasPrevious = gWork.w != 0u && all(previousCoordinate>=0) && all(previousCoordinate<int3(previousCounts));
    float4 previousSh[9];
    [unroll] for (uint c=0u;c<9u;++c) previousSh[c] = hasPrevious ? gLightProbeShVolume0[c].Load(int4(previousCoordinate,0)) : 0.0f.xxxx;
    const uint2 tileSize = uint2(HIKARI_DYNAMIC_GI_TILE_WIDTH,HIKARI_DYNAMIC_GI_TILE_HEIGHT);
    const uint2 tile = uint2(coordinate.x+coordinate.y*gCounts.x,coordinate.z)*tileSize;
    const uint2 previousTile = uint2(previousCoordinate.x+previousCoordinate.y*previousCounts.x,previousCoordinate.z)*tileSize;
    const uint previousAtlasIndex = uint(gPrevious.selectionParams.y+0.5f)-1u;
    // 物理格納順と更新順を分離し、再初期化時はカメラ付近から有効にする。
    const int3 centerDelta = int3(coordinate)-int3(gCounts.xyz/2u);
    const uint3 centerRank = min(uint3(abs(centerDelta)*2-int3(centerDelta>0)),gCounts.xyz-1u);
    const uint updateIndex = centerRank.x+centerRank.y*gCounts.x+centerRank.z*gCounts.x*gCounts.y;
    const uint selected = (updateIndex+gCounts.w-gWork.y)%gCounts.w;
    if (selected >= gWork.z)
    {
        if (lane < 9u) gOutputSh[lane][coordinate] = previousSh[lane];
        for (uint texel=lane;texel<tileSize.x*tileSize.y;texel+=64u)
        {
            const uint2 pixel=uint2(texel%tileSize.x,texel/tileSize.x);
            gOutputAtlas[tile+pixel] = hasPrevious
                ? gMaterialTexturePool[NonUniformResourceIndex(previousAtlasIndex)].Load(int3(previousTile+pixel,0)) : 0.0f.xxxx;
        }
        return;
    }
    const float maximumDistance = gPrevious.selectionParams.w;
    // 放射サンプルが球面を分担する円錐。分類 ray は点で形状を検査する。
    const float coneCosine = 1.0f - 2.0f / float(kRadianceRays);
    const float3x3 rayRotation = ProbeRayRotation(gWork.x, probeIndex);
    for (uint rayIndex = lane; rayIndex < kRayCount; rayIndex += 64u)
    {
        const float3 direction = ProbeRayDirection(rayIndex,rayRotation);
        // Visibility atlas の正規化距離は ray の打切り距離ではない。
        // 大空間の壁/屋根を打ち切って miss=sky と扱うと室内へ漏光する。
        RayDesc ray; ray.Origin=nominalPosition; ray.Direction=direction; ray.TMin=0.001f; ray.TMax=1e7f;
        HikariRaySurface hit = (HikariRaySurface)0;
        bool found = false;
        const float coneSpread = rayIndex < kClassifyRays ? 0.0f : 2.0f * sqrt(1.0f-coneCosine*coneCosine) / coneCosine;
        if (gRayLighting.scene.w == 0u) found = HikariTraceRuntimeScene(ray,gRayLighting.scene.x,gRayLighting.scene.y,gRayLighting.time,gMaterialWrap,
            HIKARI_RAY_REPORT_BACKFACES,hit,float2(0.0f,coneSpread));
        sDirection[rayIndex] = direction;
        sDistance[rayIndex] = found ? min(hit.distance,maximumDistance) : maximumDistance;
        sBackFace[rayIndex] = found ? hit.backFace : 0u;
        sSky[rayIndex] = found ? 0.0f : 1.0f;
        const float2 randomUv = frac(float2(rayIndex+1u,gWork.x+probeIndex+1u)*float2(0.754877666f,0.569840296f)+float2(gWork.x,rayIndex)*0.618033989f);
        // 分類 ray も向きと光の標本を固定して明るさを測る（球面調和には入れない）。場面が変わらなければ毎回同じ値になり、
        // 光が本当に変わったかどうかの手掛かりになる（下の履歴の重み）。
        const float2 fixedUv = frac(float2(rayIndex+1u,probeIndex+1u)*float2(0.754877666f,0.569840296f));
        sRadiance[rayIndex] = found && hit.backFace != 0u ? 0.0f.xxx
            : found ? HitRadiance(hit,-direction,rayIndex < kClassifyRays ? fixedUv : randomUv,
                hasPrevious ? previousCoordinate : int3(-1,-1,-1))
            : HikariRayEnvironmentRadiance(gRayLighting,direction,gLinearClamp);
    }
    // 隣の probe との間を影の ray で調べる。描画側は補間の基準の角から塞がれた角を混ぜない（probe 間隔より薄い壁の
    // 向こうの光は深度 moment では断てない）。影を落とさない形状は塞がない。
    if (lane < 27u)
    {
        bool open = true;
        if (lane != 13u && gRayLighting.scene.w == 0u)
        {
            const float3 delta = float3(int3(lane % 3u, (lane / 3u) % 3u, lane / 9u) - 1) * gSpacing.xyz;
            RayDesc link; link.Origin = nominalPosition; link.Direction = normalize(delta); link.TMin = 0.001f;
            link.TMax = length(delta);
            HikariRaySurface blocker;
            open = !HikariTraceRuntimeScene(link, gRayLighting.scene.x, gRayLighting.scene.y, gRayLighting.time,
                gMaterialWrap, HIKARI_RAY_SHADOW | HIKARI_RAY_REPORT_BACKFACES, blocker);
        }
        sNeighborOpen[lane] = open ? 1u : 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    // 分類 ray の多くが裏面に当たる probe は形状の中にある。動かさずに無効にする（RTXGI の非活動 probe）。
    // 薄い壁や屋根の中の probe を反対側へ押し出すと、押し出した先の光が可視性をすり抜けて室内へ漏れる。
    if (lane == 0u)
    {
        uint backFaces=0u;
        for (uint i=0u;i<kClassifyRays;++i) backFaces+=sBackFace[i]!=0u ? 1u : 0u;
        sValidity=backFaces<=4u ? 1.0f : 0.0f;
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane<9u)
    {
        float3 coefficient=0.0f.xxx;
        for(uint i=kClassifyRays;i<kRayCount;++i) coefficient+=sRadiance[i]*HikariSh9Basis(sDirection[i],lane)*(12.56637061436f/float(kRadianceRays));
        // 履歴の重みは今回の放射標本の明るさで決めない（明るい標本だけ重く混ぜると明るく、抑えると暗く偏る）。代わりに
        // 向きと光の標本を固定した分類 ray の明るさ（6 番の alpha、描画側は読まない）の変わり方で決める。固定した ray も
        // 当たった面の照り返し（隣の probe）を読むので、照り返しが主な部屋では隣の揺れの分だけ毎回動く。そこで変化の
        // 移動平均と二乗の移動平均（7・8 番の alpha）を持ち、前回までの揺れと比べて、一回で大きく変わった時（点灯・消灯）
        // か同じ向きに変わり続ける時（多重反射の積み上がり）だけ光が変わったとみなし、度合いに応じて 0.97 から 0.6 まで
        // 軽くして早く追いつく。止まった場面では 0.97 で放射標本の揺れを均す（屋根の穴の日だまりを拾う本数が毎回変わり、
        // 0.95 では壁の明るさが 8 フレームごとに段になって変わり続けた）。固定の 0.95 では白い壁の通路で、相機が
        // 止まってからも約 15 秒明るくなり続けた。更新回数（5 番の alpha）で、新しい probe は最初の数回を単純平均にする。
        const bool continuing=previousSh[0].a>0.0f && sValidity>0.0f;
        const float updates=continuing ? previousSh[5].a : 0.0f;
        float fixedLevel=0.0f;
        for(uint f=0u;f<kClassifyRays;++f) fixedLevel+=dot(sRadiance[f],float3(0.2126f,0.7152f,0.0722f))/float(kClassifyRays);
        const float fixedPrevious=continuing ? previousSh[6].a : fixedLevel;
        const float lightStep=(fixedLevel-fixedPrevious)/max(max(fixedLevel,fixedPrevious),1.0e-4f);
        const float trendPrevious=continuing ? previousSh[7].a : 0.0f;
        const float squarePrevious=continuing ? previousSh[8].a : 0.0f;
        const float lightTrend=lerp(trendPrevious,lightStep,0.3f);
        const float lightSquare=lerp(squarePrevious,lightStep*lightStep,0.3f);
        const float spread=sqrt(max(squarePrevious-trendPrevious*trendPrevious,0.0f));
        // 重み 0.3 の移動平均の標準誤差は揺れの約 0.42 倍。
        const float lightChange=max(
            smoothstep(3.0f,5.0f,abs(lightTrend)/(0.42f*spread+0.005f))*smoothstep(0.01f,0.03f,abs(lightTrend)),
            smoothstep(4.0f,6.0f,abs(lightStep)/(spread+0.01f))*smoothstep(0.1f,0.3f,abs(lightStep)));
        const float history=continuing ? min(lerp(0.97f,0.6f,lightChange),updates/(updates+1.0f)) : 0.0f;
        // 0 番の alpha は validity、1..4 番は空の見える割合の L1 SH（全 ray）。描画側の空の鏡面反射を遮る。
        float sky=0.0f;
        if (lane>=1u && lane<=4u)
        {
            for(uint k=0u;k<kRayCount;++k) sky+=sSky[k]*HikariSh9Basis(sDirection[k],lane-1u)*(12.56637061436f/float(kRayCount));
            sky=lerp(sky,previousSh[lane].a,history);
            sSkySh[lane-1u]=sky;
        }
        const float alpha=lane==0u ? sValidity : (lane==5u ? min(updates+1.0f,64.0f) : (lane==6u ? fixedLevel : (lane==7u ? lightTrend : (lane==8u ? lightSquare : sky))));
        sSh[lane]=lerp(coefficient,previousSh[lane].rgb,history);
        gOutputSh[lane][coordinate]=float4(sSh[lane],alpha);
    }
    GroupMemoryBarrierWithGroupSync();
    const bool keepMoments=hasPrevious && previousSh[0].a>0.0f && sValidity>0.0f;
    // 深度 moment の履歴も更新回数で始めは軽くし、RTXGI と同じ 0.97 まで上げる。一回の更新の ray は RTXGI の半分で、
    // 0.9 では屋根の縁を抜ける疎な遠い ray の有無で壁の向こうの probe の可視性が更新ごとに揺れ、壁の上端が点滅した。
    const float momentHistory=keepMoments ? min(0.97f,previousSh[5].a/(previousSh[5].a+1.0f)) : 0.0f;
    // 描画側が probe ごとに 9 係数を読まないよう、放射照度 E/pi を 8x8 の八面体へ展開する。
    for(uint texel=lane;texel<100u;texel+=64u)
    {
        const uint2 pixel=uint2(texel%10u,texel/10u);
        const float3 axis=HikariDecodeOctahedralDirection((float2(HikariWrapOctahedralTexel(int2(pixel)-1,8))+0.5f)/8.0f);
        float3 irradiance=0.0f.xxx;
        [unroll] for(uint c=0u;c<9u;++c) irradiance+=sSh[c]*HikariSh9DiffuseWeight(axis,c);
        float skyVisibility=0.0f;
        [unroll] for(uint b=0u;b<4u;++b) skyVisibility+=sSkySh[b]*HikariSh9DiffuseWeight(axis,b);
        // 放射照度は 1/5 乗で保存する（RTXGI の gamma 5。暗い所の精度）。描画側は 1/2 乗の空間で補間する
        // （HikariDynamicGiBlendSpace）。
        gOutputAtlas[tile+pixel]=float4(pow(clamp(irradiance,0.0f,65000.0f),1.0f/kIrradianceGamma),saturate(skyVisibility));
    }
    // 深度 moment は 16x16 の八面体。波瓣は鋭く取る（DDGI と同じ cos^50）。広いと壁に沿って抜ける長い ray が隣の方向へ
    // 混ざり、壁の裏の probe が斜めから見えると判定されて外の光が漏れる。
    for(uint texel=lane;texel<324u;texel+=64u)
    {
        const uint2 pixel=uint2(texel%18u,texel/18u);
        const float3 axis=HikariDecodeOctahedralDirection((float2(HikariWrapOctahedralTexel(int2(pixel)-1,16))+0.5f)/16.0f);
        float weightSum=0.0f; float2 moments=0.0f.xx;
        for(uint i=0u;i<kRayCount;++i)
        {
            const float weight=pow(saturate(dot(axis,sDirection[i])),50.0f);
            const float distance=sDistance[i]/maximumDistance;
            moments+=float2(distance,distance*distance)*weight; weightSum+=weight;
        }
        const float2 fresh=moments/max(weightSum,1e-20f);
        // 一回の更新の ray は少なく、向きは更新ごとに回る。probe が動かない間は前回の moment と重ね、
        // 17 度以内に ray が来なかった方向は前回を保つ。新しい probe や無効だった probe では前回を捨てる。
        // 波瓣の中心に十分な ray があって前回の平均より揺れの幅を大きく超えて近い時は、遮るもの（閉じた扉など）が現れた
        // とみなして早く追う（遠い深度のまま漏れ続けない）。遠くなる向きは疎な ray の揺れと区別できないので遅いまま。
        const uint2 momentTexel=uint2(10u,0u)+pixel;
        float2 result=fresh;
        if (keepMoments)
        {
            const float2 previousMoments=gMaterialTexturePool[NonUniformResourceIndex(previousAtlasIndex)].Load(int3(previousTile+momentTexel,0)).rg;
            const float previousSpread=sqrt(max(previousMoments.y-previousMoments.x*previousMoments.x,0.0f));
            const bool occluded=weightSum>=0.5f && fresh.x<previousMoments.x-3.0f*previousSpread-0.05f;
            result=weightSum>=0.1f ? lerp(fresh,previousMoments,occluded ? 0.5f : momentHistory) : previousMoments;
        }
        const float links = pixel.y == 1u && pixel.x >= 1u && pixel.x <= 8u ? float(ProbeOctantLinks(pixel.x - 1u)) : 0.0f;
        gOutputAtlas[tile+momentTexel]=float4(result,sValidity,links);
    }
}
