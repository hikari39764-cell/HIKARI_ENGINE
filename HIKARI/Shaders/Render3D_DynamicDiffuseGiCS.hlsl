#include "Include/Lighting/HIKARI_RuntimeRayLighting.hlsli"
#include "Include/Lighting/HIKARI_IrradianceSh9.hlsli"

SamplerState gLinearClamp : register(s0);
SamplerState gMaterialWrap : register(s1);
#include "Include/Lighting/HIKARI_DynamicGiSampling.hlsli"
RWTexture2D<float4> gOutputAtlas : register(u0);
// 近景の置き場所を決める相機の ray（中央・右・左・上・下・真下）が当たった点の高さ（当たらなければ 1e30）と、
// その後ろ 6 個に相機からの水平距離。
RWStructuredBuffer<float> gFocusHeights : register(u1);

cbuffer DynamicDiffuseGiCB : register(b0)
{
    HikariIrradianceVolumeData gPreviousVolumes[HIKARI_DYNAMIC_GI_VOLUME_COUNT];
    uint4 gField;           // x この層の index
    float4 gOrigin;         // xyz 格子の原点
    float4 gSpacing;        // xyz 軸ごとの間隔
    uint4 gCounts;          // xyz counts, w total
    uint4 gWork;            // x sequence, y first probe, z budget, w 1 なら前の格子と互換（履歴を引き継ぐ）
    HikariRayLightingData gRayLighting;
    float4 gFocusOrigin;    // xyz 相機の位置、w は近景の更新で 1（相機の ray を撃つ）
    float4 gFocusDirection; // xyz 視線の向き
    uint4 gPriorityOrigin;  // xyz 格子範囲の始点、w 追加更新の上限
    uint4 gPrioritySize;    // xyz 格子範囲の大きさ、w 巡回の開始点
    float4 gBlend;          // x 収束後の履歴の重み、yz 固定 ray の明るさの相対変化で履歴を弱める範囲、w 収束とみなす変動
    float4 gRelocate;       // x 面からの最小距離 [m]、y 裏面率の閾値、z 移動の上限（間隔の割合）、w 形状が近いとみなす距離 [m]
};

#define gPrevious gPreviousVolumes[gField.x]

// 前半は固定方向の ray（RTXGI の fixed rays）。再配置・分類・深度・円盤・光照の変化の検出に使い、放射照度には入れない。
// 後半は更新ごとに回す放射サンプル。放射照度は余弦の重みで八面体の texel へ直接積み（DDGI の方式）、球面調和を介さない。
static const uint kFixedRays = 64u, kRadianceRays = 128u, kRayCount = kFixedRays + kRadianceRays;
groupshared float3 sDirection[kRayCount];
groupshared float3 sRadiance[kRayCount];
groupshared float sDistance[kRayCount]; // 命中までの距離 [m]。未命中は 1e30。
groupshared uint sBackFace[kRayCount];
groupshared float sSky[kRayCount];
groupshared float3 sTriangleA[kFixedRays];
groupshared float3 sTriangleB[kFixedRays];
groupshared float3 sTriangleC[kFixedRays];
groupshared float3 sIrradianceTexel[64];
groupshared float sSkyTexel[64];
groupshared float sVariation[64];
groupshared float3 sOffset;
groupshared float sValidity;
groupshared float sBackRatio;
groupshared float sFixedLevel;

uint ProbeHash(uint value)
{
    value = value * 747796405u + 2891336453u;
    value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    return (value >> 22u) ^ value;
}

// 放射サンプルの球面 Fibonacci を更新ごと・probe ごとに一様な乱数回転で回す（Shoemake の単位四元数）。
// 方位角だけ回すと仰角の帯が毎回同じになり、深度と放射照度の推定に偏りが残る。固定 ray は回さない。
float3x3 ProbeRayRotation(uint sequence, uint probeSeed)
{
    const uint seed = ProbeHash(sequence * 9781u + probeSeed * 6271u + 1u);
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
    const bool fixedRay = rayIndex < kFixedRays;
    const uint count = fixedRay ? kFixedRays : kRadianceRays;
    const uint index = fixedRay ? rayIndex : rayIndex - kFixedRays;
    const float y = 1.0f - 2.0f * (float(index) + 0.5f) / float(count);
    const float angle = 2.39996322973f * float(index);
    const float radius = sqrt(saturate(1.0f - y * y));
    const float3 direction = float3(cos(angle) * radius, y, sin(angle) * radius);
    return fixedRay ? direction : mul(rotation, direction);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// 命中した位置の間接光は全層の前フレームから読む。覆っていない分はその向きのぼかした空で近似する（開けた所の遠い命中）。
float3 HitRadiance(HikariRaySurface hit, float3 view, float2 randomUv)
{
    const HikariDynamicGiSample previous = HikariQueryDynamicGi(gPreviousVolumes,
        HikariSurfaceGiQuery(hit.position, hit.geometricNormal, hit.normal, view));
    const float3 incident = previous.value +
        HikariRayEnvironmentRadiance(gRayLighting, hit.normal, gLinearClamp, 4.0f) * (1.0f - previous.coverage);
    return HikariRayHitRadiance(gRayLighting, hit, view, randomUv, incident, 0.0f.xxx, false, gMaterialWrap);
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
    const uint3 coordinate = uint3(probeIndex % gCounts.x, (probeIndex / gCounts.x) % gCounts.y,
        probeIndex / (gCounts.x * gCounts.y));
    const float3 nominalPosition = gOrigin.xyz + float3(coordinate) * gSpacing.xyz;
    // 前の格子は自分の間隔と数で引く（画質の档位を切り替えた直後は前の格子と間隔も数も違い、近い probe を引き継ぐ）。
    const uint3 previousCounts = uint3(gPrevious.countsProbeCount.xyz + 0.5f);
    const int3 previousCoordinate =
        int3(round((nominalPosition - gPrevious.originSampling.xyz) / gPrevious.spacingIntensity.xyz));
    const bool hasPrevious = gWork.w != 0u && all(previousCoordinate >= 0) && all(previousCoordinate < int3(previousCounts));
    const uint2 tileSize = uint2(HIKARI_DYNAMIC_GI_TILE_WIDTH, HIKARI_DYNAMIC_GI_TILE_HEIGHT);
    const uint2 tile = uint2(coordinate.x + coordinate.y * gCounts.x, coordinate.z) * tileSize;
    const uint2 previousTile =
        uint2(previousCoordinate.x + previousCoordinate.y * previousCounts.x, previousCoordinate.z) * tileSize;
    const uint previousAtlasIndex = uint(gPrevious.selectionParams.y + 0.5f) - 1u;
    float4 previousState = 0.0f.xxxx, previousStatistics = 0.0f.xxxx;
    if (hasPrevious)
    {
        previousState = gMaterialTexturePool[NonUniformResourceIndex(previousAtlasIndex)].Load(
            int3(int2(previousTile) + HIKARI_DYNAMIC_GI_STATE_TEXEL, 0));
        previousStatistics = gMaterialTexturePool[NonUniformResourceIndex(previousAtlasIndex)].Load(
            int3(int2(previousTile) + HIKARI_DYNAMIC_GI_STATISTICS_TEXEL, 0));
    }
    const bool initialized = hasPrevious && previousState.a > 0.0f;
    // 格子点からの移動は非活性の間も保つ（壁の中へ戻さない）。
    const float3 offset = hasPrevious ? previousState.rgb : 0.0f.xxx;
    const float updates = initialized ? previousStatistics.r : 0.0f;
    const float variation = initialized ? previousStatistics.g : 1.0f;
    // 物理格納順と更新順を分離し、再初期化時はカメラ付近から有効にする。
    const int3 centerDelta = int3(coordinate) - int3(gCounts.xyz / 2u);
    const uint3 centerRank = min(uint3(abs(centerDelta) * 2 - int3(centerDelta > 0)), gCounts.xyz - 1u);
    const uint updateIndex = centerRank.x + centerRank.y * gCounts.x + centerRank.z * gCounts.x * gCounts.y;
    const uint selected = (updateIndex + gCounts.w - gWork.y) % gCounts.w;
    const uint3 priorityCell = coordinate - gPriorityOrigin.xyz;
    const uint priorityCount = gPrioritySize.x * gPrioritySize.y * gPrioritySize.z;
    const bool affected = gPriorityOrigin.w > 0u && all(coordinate >= gPriorityOrigin.xyz) &&
        all(priorityCell < gPrioritySize.xyz);
    const uint priorityRank = priorityCell.x + gPrioritySize.x * (priorityCell.y + gPrioritySize.y * priorityCell.z);
    const bool priorityUpdate = affected &&
        (priorityRank + priorityCount - gPrioritySize.w) % max(priorityCount, 1u) < gPriorityOrigin.w;
    // 収束した probe（RTXGI の probe variability）は 4 回に 1 回だけ更新し、光照が変われば固定 ray の明るさで目を覚ます。
    const bool converged = initialized && updates >= 16.0f && variation < gBlend.w;
    const bool resting = converged && (ProbeHash(gWork.x * 2654435761u ^ probeIndex) & 3u) != 0u;
    // 新しく視野へ入った格子も待たせない。既存格子は変更の近傍だけ先行更新する。
    if (hasPrevious && !priorityUpdate && (selected >= gWork.z || resting))
    {
        for (uint texel = lane; texel < tileSize.x * tileSize.y; texel += 64u)
        {
            const uint2 pixel = uint2(texel % tileSize.x, texel / tileSize.x);
            gOutputAtlas[tile + pixel] =
                gMaterialTexturePool[NonUniformResourceIndex(previousAtlasIndex)].Load(int3(previousTile + pixel, 0));
        }
        return;
    }
    const float maximumDistance = gPrevious.selectionParams.w;
    const float3 probePosition = nominalPosition + offset;
    // 放射サンプルが球面を分担する円錐。固定 ray は点で形状を検査する。
    const float coneCosine = 1.0f - 2.0f / float(kRadianceRays);
    // 格子がスクロールしても同じ世界位置の乱数列と固定の測光標本を保つ。
    const int3 worldCell = int3(floor(nominalPosition / gSpacing.xyz));
    const uint probeSeed = ProbeHash(asuint(worldCell.x)) ^ ProbeHash(asuint(worldCell.y) + 6271u) ^
        ProbeHash(asuint(worldCell.z) + 9781u);
    const uint sampleSeed = probeSeed & 0xffffu;
    const float3x3 rayRotation = ProbeRayRotation(gWork.x, probeSeed);
    for (uint rayIndex = lane; rayIndex < kRayCount; rayIndex += 64u)
    {
        const bool fixedRay = rayIndex < kFixedRays;
        const float3 direction = ProbeRayDirection(rayIndex, rayRotation);
        // Visibility atlas の正規化距離は ray の打切り距離ではない。
        // 大空間の壁/屋根を打ち切って miss=sky と扱うと室内へ漏光する。
        RayDesc ray; ray.Origin = probePosition; ray.Direction = direction; ray.TMin = 0.001f; ray.TMax = 1e7f;
        HikariRaySurface hit = (HikariRaySurface)0;
        bool found = false;
        const float coneSpread = fixedRay ? 0.0f : 2.0f * sqrt(1.0f - coneCosine * coneCosine) / coneCosine;
        if (gRayLighting.scene.w == 0u) found = HikariTraceRuntimeScene(ray, gRayLighting.scene.x, gRayLighting.scene.y,
            gRayLighting.time, gMaterialWrap, HIKARI_RAY_REPORT_BACKFACES, hit, float2(0.0f, coneSpread));
        sDirection[rayIndex] = direction;
        sDistance[rayIndex] = found ? hit.distance : 1.0e30f;
        sBackFace[rayIndex] = found ? hit.backFace : 0u;
        sSky[rayIndex] = found ? 0.0f : 1.0f;
        if (fixedRay)
        {
            const bool solid = found && hit.distance < maximumDistance &&
                (HikariGetMeshMaterialData(hit.materialIndex).materialFlags & HIKARI_MATERIAL_FLAG_ALPHA_MASK) == 0u;
            sTriangleA[rayIndex] = solid ? (hit.trianglePosition[0] - probePosition) / maximumDistance : 0.0f.xxx;
            sTriangleB[rayIndex] = solid ? (hit.trianglePosition[1] - probePosition) / maximumDistance : 0.0f.xxx;
            sTriangleC[rayIndex] = solid ? (hit.trianglePosition[2] - probePosition) / maximumDistance : 0.0f.xxx;
        }
        const float2 randomUv = frac(float2(rayIndex + 1u, gWork.x + sampleSeed + 1u) *
            float2(0.754877666f, 0.569840296f) + float2(gWork.x, rayIndex) * 0.618033989f);
        // 固定 ray は光の標本も固定して明るさを測る。場面が変わらなければ毎回同じ値になり、光が本当に変わったかの
        // 手掛かりになる（履歴の重み）。
        const float2 fixedUv = frac(float2(rayIndex + 1u, sampleSeed + 1u) * float2(0.754877666f, 0.569840296f));
        sRadiance[rayIndex] = found && hit.backFace != 0u ? 0.0f.xxx
            : found ? HitRadiance(hit, -direction, fixedRay ? fixedUv : randomUv)
            : HikariRayEnvironmentRadiance(gRayLighting, direction, gLinearClamp);
    }
    GroupMemoryBarrierWithGroupSync();
    // 再配置と分類（RTXGI の probe relocation / classification）。固定 ray の多くが裏面に当たる probe は形状の中：
    // 最も近い裏面を抜けた先へ移す。面に近すぎる probe は最も遠い面の向きへ離す。十分離れていれば格子点へ戻る。
    if (lane == 0u)
    {
        uint backfaces = 0u;
        float closestBack = 1.0e30f, closestFront = 1.0e30f, farthestFront = 0.0f;
        float3 closestBackDirection = 0.0f.xxx, closestFrontDirection = 0.0f.xxx, farthestFrontDirection = 0.0f.xxx;
        float level = 0.0f;
        for (uint i = 0u; i < kFixedRays; ++i)
        {
            level += Luminance(sRadiance[i]) / float(kFixedRays);
            if (sDistance[i] >= 1.0e29f) continue;
            if (sBackFace[i] != 0u)
            {
                ++backfaces;
                if (sDistance[i] < closestBack) { closestBack = sDistance[i]; closestBackDirection = sDirection[i]; }
            }
            else
            {
                if (sDistance[i] < closestFront) { closestFront = sDistance[i]; closestFrontDirection = sDirection[i]; }
                if (sDistance[i] > farthestFront) { farthestFront = sDistance[i]; farthestFrontDirection = sDirection[i]; }
            }
        }
        const float backRatio = float(backfaces) / float(kFixedRays);
        const float minimumFront = gRelocate.x;
        float3 next = offset;
        if (backRatio > gRelocate.y)
            next = offset + closestBackDirection * (closestBack + minimumFront * 0.5f);
        else if (closestFront < minimumFront)
        {
            if (dot(farthestFrontDirection, closestFrontDirection) <= 0.0f)
                next = offset + farthestFrontDirection * min(farthestFront, minimumFront);
        }
        else if (closestFront > minimumFront && dot(offset, offset) > 1.0e-8f)
            next = offset - normalize(offset) * min(closestFront - minimumFront, length(offset));
        const float3 limit = gRelocate.z * gSpacing.xyz;
        sOffset = clamp(next, -limit, limit);
        sValidity = backRatio <= gRelocate.y && closestFront < gRelocate.w ? 1.0f : 0.0f;
        sBackRatio = backRatio;
        sFixedLevel = level;
    }
    GroupMemoryBarrierWithGroupSync();
    const bool continuing = initialized && sValidity > 0.0f;
    // 履歴の重みは固定 ray の明るさの相対変化で弱める（点灯・消灯に追いつく）。放射標本の揺れでは決めない。
    const float previousLevel = continuing ? previousStatistics.a : sFixedLevel;
    const float lightChange = smoothstep(gBlend.y, gBlend.z,
        abs(sFixedLevel - previousLevel) / max(max(sFixedLevel, previousLevel), 1.0e-4f));
    const float hysteresis = continuing ? min(lerp(gBlend.x, 0.5f, lightChange), updates / (updates + 1.0f)) : 0.0f;
    // 放射照度：lane ごとに八面体の 1 texel。余弦の重みで放射標本を積み、E/pi を 1/5 乗で保存して履歴と混ぜる。
    {
        const uint2 pixel = uint2(lane % 8u, lane / 8u);
        const float3 axis = HikariDecodeOctahedralDirection((float2(pixel) + 0.5f) / 8.0f);
        float3 sum = 0.0f.xxx;
        float sky = 0.0f, weightSum = 0.0f;
        for (uint i = kFixedRays; i < kRayCount; ++i)
        {
            const float weight = max(0.0f, dot(axis, sDirection[i]));
            sum += sRadiance[i] * weight;
            sky += sSky[i] * weight;
            weightSum += weight;
        }
        const float3 encoded = pow(clamp(sum / max(weightSum, 1.0e-6f), 0.0f, 65000.0f), 1.0f / kIrradianceGamma);
        const float skyVisibility = sky / max(weightSum, 1.0e-6f);
        float4 previous = float4(encoded, skyVisibility);
        float blend = hysteresis;
        if (continuing)
        {
            previous = gMaterialTexturePool[NonUniformResourceIndex(previousAtlasIndex)].Load(
                int3(int2(previousTile) + 1 + int2(pixel), 0));
        }
        else
        {
            // 新しい probe は粗い層（遠景）から初期値を取り、最初の一回で半分だけ更新する（粗から細）。
            const HikariDynamicGiSample coarse = HikariQueryDynamicGi(gPreviousVolumes,
                HikariSurfaceGiQuery(probePosition, axis, axis));
            if (coarse.coverage > 0.5f)
            {
                previous = float4(pow(clamp(coarse.value / coarse.coverage, 0.0f, 65000.0f), 1.0f / kIrradianceGamma),
                    saturate(coarse.skyVisibility / coarse.coverage));
                blend = 0.5f;
            }
        }
        const float3 blended = lerp(encoded, previous.rgb, blend);
        sIrradianceTexel[lane] = blended;
        sSkyTexel[lane] = lerp(skyVisibility, previous.a, blend);
        sVariation[lane] = continuing ? abs(Luminance(blended) - Luminance(previous.rgb)) /
            max(Luminance(previous.rgb), 0.02f) : 1.0f;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint texel = lane; texel < 100u; texel += 64u)
    {
        const uint2 pixel = uint2(texel % 10u, texel / 10u);
        const int2 source = HikariWrapOctahedralTexel(int2(pixel) - 1, 8);
        const uint index = uint(source.y * 8 + source.x);
        gOutputAtlas[tile + pixel] = float4(sIrradianceTexel[index], saturate(sSkyTexel[index]));
    }
    // 深度 moment は 16x16 の八面体。波瓣は鋭く取る（DDGI と同じ cos^50）。広いと壁に沿って抜ける長い ray が隣の
    // 方向へ混ざり、壁の裏の probe が斜めから見えると判定されて外の光が漏れる。裏面の命中は 0.2 倍（RTXGI）。
    // 幾何が動いた範囲は古い深度を混ぜない。
    const float momentBlend = continuing ? min(affected ? 0.5f : gBlend.x, updates / (updates + 1.0f)) : 0.0f;
    for (uint texel = lane; texel < 324u; texel += 64u)
    {
        const uint2 pixel = uint2(texel % 18u, texel / 18u);
        const float3 axis =
            HikariDecodeOctahedralDirection((float2(HikariWrapOctahedralTexel(int2(pixel) - 1, 16)) + 0.5f) / 16.0f);
        float weightSum = 0.0f;
        float2 moments = 0.0f.xx;
        for (uint i = 0u; i < kRayCount; ++i)
        {
            const float weight = pow(saturate(dot(axis, sDirection[i])), 50.0f);
            const float distance = min(sDistance[i] * (sBackFace[i] != 0u ? 0.2f : 1.0f), maximumDistance) / maximumDistance;
            moments += float2(distance, distance * distance) * weight;
            weightSum += weight;
        }
        float2 result = moments / max(weightSum, 1.0e-20f);
        if (continuing)
        {
            const float2 previous = gMaterialTexturePool[NonUniformResourceIndex(previousAtlasIndex)].Load(
                int3(int2(previousTile) + HIKARI_DYNAMIC_GI_DEPTH_ORIGIN + int2(pixel), 0)).rg;
            result = lerp(result, previous, momentBlend);
        }
        gOutputAtlas[tile + uint2(HIKARI_DYNAMIC_GI_DEPTH_ORIGIN) + pixel] = float4(result, sValidity, 0.0f);
    }
    // 固定 ray が当たった三角形に内接する円盤。無限平面は柱の輪郭の外まで遮ってしまう。
    {
        const uint2 pixel = uint2(lane % 8u, lane / 8u);
        const float3 axis = HikariDecodeOctahedralDirection((float2(pixel) + 0.5f) / 8.0f);
        float best = -1.0f;
        uint closest = 0u;
        for (uint i = 0u; i < kFixedRays; ++i)
        {
            const float alignment = dot(axis, sDirection[i]);
            if (alignment > best) { best = alignment; closest = i; }
        }
        const float3 a = sTriangleA[closest], b = sTriangleB[closest], c = sTriangleC[closest];
        const float3 crossEdges = cross(b - a, c - a);
        const float area = length(crossEdges);
        float4 patch = 0.0f.xxxx;
        if (area > 1e-10f)
        {
            const float3 normal = crossEdges / area;
            const float distance = dot(normal, a);
            const float cosine = dot(normal, axis);
            if (abs(cosine) > 1e-5f && distance / cosine > 0.0f)
            {
                const float3 center = axis * (distance / cosine);
                const float radius = min(min(dot(cross(b - a, center - a), normal) / length(b - a),
                    dot(cross(c - b, center - b), normal) / length(c - b)), dot(cross(a - c, center - c), normal) / length(a - c));
                if (radius > 0.002f)
                    patch = float4(HikariEncodeOctahedralDirection(normal), distance, min(radius, 65000.0f));
            }
        }
        gOutputAtlas[tile + uint2(HIKARI_DYNAMIC_GI_PATCH_ORIGIN) + pixel] = patch;
    }
    // 状態の列。変動は texel ごとの相対変化の平均の移動平均（収束の判定）。
    if (lane < 36u)
    {
        const uint2 pixel = uint2(28u + lane / 18u, lane % 18u);
        float4 value = 0.0f.xxxx;
        if (lane == 0u)
            value = float4(sOffset, sValidity > 0.0f ? saturate((updates + 1.0f) / 8.0f) : 0.0f);
        else if (lane == 1u)
        {
            float change = 0.0f;
            for (uint i = 0u; i < 64u; ++i) change += sVariation[i] / 64.0f;
            const float nextVariation = continuing ? lerp(variation, change, 0.3f) : 1.0f;
            value = float4(min(updates + 1.0f, 255.0f), nextVariation, sBackRatio, sFixedLevel);
        }
        gOutputAtlas[tile + pixel] = value;
    }
}
