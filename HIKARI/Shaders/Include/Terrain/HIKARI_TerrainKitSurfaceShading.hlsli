#ifndef HIKARI_TERRAINKIT_SURFACE_SHADING_INCLUDED
#define HIKARI_TERRAINKIT_SURFACE_SHADING_INCLUDED

// 地形モジュールの地表の色。Render3D_TerrainKitSurface.hlsl の資源と TerrainVertex を使う。
// ---- 地表材質 ----
// 去重復：Mikkelsen 2022「Practical Real-Time Hex-Tiling」の六角タイル（3 標本を高さで鋭く混ぜる）。
// 崖：Quilez の biplanar の考え方で、主な側面 2 つだけを読む。法線は whiteout（Golus）で組む。
// 色は色の図で決まるので、材質の図（濃淡・法線・粗さ）を読むのは近くだけでよい。
static const float kFarStartMeters = 140.0f;
static const float kFarFadeMeters = 60.0f;
static const float kColorMapSize = 257.0f;
static const float kFarRoughness = 0.85f;
// 細部（一番小さい繰り返し）を読む距離。ここより遠くは中と大の尺度だけで描く。
static const float kDetailStartMeters = 12.0f;
static const float kDetailEndMeters = 40.0f;
// 同じ図を大きな尺度でも読む（UE の距離ブレンドと Terrain3D のマクロ変化の考え方）。尺度の比を割り切れない値にして、
// 回転もずらし、繰り返しが揃わないようにする。
static const float kMidScale = 6.1f;
static const float kLargeScale = 29.3f;
static const float kMidNormalStrength = 0.9f;
static const float2x2 kMidRotation = float2x2(0.7986355f, -0.6018150f, 0.6018150f, 0.7986355f);
static const float2x2 kLargeRotation = float2x2(0.3255682f, -0.9455186f, 0.9455186f, 0.3255682f);

struct TerrainSurfaceSample
{
    float3 albedo; // 材質の段階では色の図に掛ける濃淡（平均が 1）
    float height;
    float3 normal;
    float roughness;
    float ao;
};

float2 TerrainHash2(int2 cell)
{
    const float2 p = float2(dot(float2(cell), float2(127.1f, 311.7f)), dot(float2(cell), float2(269.5f, 183.3f)));
    return frac(sin(p) * 43758.5453f);
}

float3 TerrainWhiteoutTop(float2 tangent, float strength, float3 macroNormal)
{
    const float2 xy = tangent * strength;
    const float z = sqrt(saturate(1.0f - dot(xy, xy)));
    return float3(xy.x + macroNormal.x, z * macroNormal.y, xy.y + macroNormal.z);
}

// 一つの投影面を六角タイルで読む。detail の xy は uv の向きの法線。
void TerrainSamplePlanar(float layer, float2 uv, float2 duvdx, float2 duvdy, out float4 color, out float4 detail)
{
    // 三角格子（Heitz & Neyret の座標）。
    const float2x2 gridToSkewed = float2x2(1.0f, -0.57735027f, 0.0f, 1.15470054f);
    const float2 skewed = mul(gridToSkewed, uv * 3.46410162f);
    const int2 base = int2(floor(skewed));
    const float3 bary = float3(frac(skewed), 0.0f);
    const float third = 1.0f - bary.x - bary.y;
    const float side = third < 0.0f ? 1.0f : 0.0f;
    const float sign2 = 2.0f * side - 1.0f;
    float3 weights = float3(-third * sign2, side - bary.y * sign2, side - bary.x * sign2);
    const int sideInt = int(side);
    const int2 vertices[3] = {base + int2(sideInt, sideInt), base + int2(sideInt, 1 - sideInt), base + int2(1 - sideInt, sideInt)};
    float4 hexColor[3];
    float4 hexDetail[3];
    for (uint i = 0u; i < 3u; ++i)
    {
        const float2 hash = TerrainHash2(vertices[i]);
        const float angle = hash.x * 6.2831853f;
        const float2 axis = float2(cos(angle), sin(angle));
        const float2x2 rotation = float2x2(axis.x, -axis.y, axis.y, axis.x);
        const float2 tileUv = mul(rotation, uv) + hash;
        const float2 dx = mul(rotation, duvdx);
        const float2 dy = mul(rotation, duvdy);
        hexColor[i] = gTerrainSurfaceColor.SampleGrad(gLinearWrap, float3(tileUv, layer), dx, dy);
        hexDetail[i] = gTerrainSurfaceDetail.SampleGrad(gLinearWrap, float3(tileUv, layer), dx, dy);
        // 接線の法線を世界の向きへ戻す（回したぶんを逆に）。
        const float2 tangent = hexDetail[i].xy * 2.0f - 1.0f;
        hexDetail[i].xy = mul(tangent, rotation) * 0.5f + 0.5f;
    }
    // 高さで鋭くして、混ざって眠くなるのを防ぐ。
    weights = pow(max(weights * (0.4f + float3(hexColor[0].a, hexColor[1].a, hexColor[2].a)), 1.0e-4f), 7.0f);
    weights /= weights.x + weights.y + weights.z;
    color = hexColor[0] * weights.x + hexColor[1] * weights.y + hexColor[2] * weights.z;
    detail = hexDetail[0] * weights.x + hexDetail[1] * weights.y + hexDetail[2] * weights.z;
}

// 一つの投影面の濃淡・高さ・法線。材質の図は平均色に対する濃淡として読み、色そのものは色の図に任せる。
// 細部（一番小さい繰り返し）は近くでだけ読む。遠くの細部は平均に収束するので、読まなくても続く。
struct TerrainPlaneSample
{
    float4 color;
    float2 tangent;
    float2 roughnessAo;
};

TerrainPlaneSample TerrainSamplePlane(float layer, float2 uv, float2 duvdx, float2 duvdy, float3 mean, float variation, float detailWeight)
{
    const float2 midUv = mul(kMidRotation, uv) / kMidScale;
    const float2 midDx = mul(kMidRotation, duvdx) / kMidScale;
    const float2 midDy = mul(kMidRotation, duvdy) / kMidScale;
    const float2 largeUv = mul(kLargeRotation, uv) / kLargeScale;
    const float2 largeDx = mul(kLargeRotation, duvdx) / kLargeScale;
    const float2 largeDy = mul(kLargeRotation, duvdy) / kLargeScale;
    const float4 mid = gTerrainSurfaceColor.SampleGrad(gLinearWrap, float3(midUv, layer), midDx, midDy);
    const float3 large = gTerrainSurfaceColor.SampleGrad(gLinearWrap, float3(largeUv, layer), largeDx, largeDy).rgb;
    const float4 midDetail = gTerrainSurfaceDetail.SampleGrad(gLinearWrap, float3(midUv, layer), midDx, midDy);
    const float3 ratio = clamp(1.0f + (0.5f * (mid.rgb + large) / max(mean, 0.002f) - 1.0f) * variation, 0.4f, 1.8f);
    TerrainPlaneSample result;
    result.color = float4(ratio, mid.a);
    result.tangent = mul(midDetail.xy * 2.0f - 1.0f, kMidRotation) * kMidNormalStrength;
    result.roughnessAo = midDetail.zw;
    if (detailWeight > 0.0f)
    {
        float4 color;
        float4 detail;
        TerrainSamplePlanar(layer, uv, duvdx, duvdy, color, detail);
        result.color = lerp(result.color, float4(color.rgb / max(mean, 0.002f) * ratio, color.a), detailWeight);
        result.tangent += (detail.xy * 2.0f - 1.0f) * detailWeight;
        result.roughnessAo = lerp(result.roughnessAo, detail.zw, detailWeight);
    }
    return result;
}

TerrainSurfaceSample TerrainSampleSurface(uint surface, float3 worldPosition, float3 dpdx, float3 dpdy, float3 macroNormal, float cliff, float detailWeight)
{
    const float4 params = gTerrainSurfaceParams.Load(int4(surface, 0, 0, 0));
    const float4 tone = gTerrainSurfaceParams.Load(int4(surface, 1, 0, 0));
    const float tiling = max(params.x, 0.01f);
    const float layer = (float)surface;
    float4 color = 0.0f.xxxx;
    float3 normal = 0.0f.xxx;
    float2 roughnessAo = 0.0f.xx;
    if (cliff < 1.0f)
    {
        const TerrainPlaneSample top = TerrainSamplePlane(layer, worldPosition.xz / tiling, dpdx.xz / tiling, dpdy.xz / tiling, tone.rgb, tone.a, detailWeight);
        const float weight = 1.0f - cliff;
        color += top.color * weight;
        normal += TerrainWhiteoutTop(top.tangent, params.z, macroNormal) * weight;
        roughnessAo += top.roughnessAo * weight;
    }
    if (cliff > 0.0f)
    {
        // 主な側面 2 つ（x 向きと z 向き）を面の向きで混ぜる。図の v は世界の下向き。
        float2 sideWeights = pow(abs(macroNormal.xz), 4.0f);
        sideWeights /= max(sideWeights.x + sideWeights.y, 1.0e-4f);
        // 両方を読むのは境の狭い帯だけ（読む回数を減らす）。
        sideWeights = saturate((sideWeights - 0.15f) / 0.7f);
        float3 sideNormal = 0.0f.xxx;
        float4 sideColor = 0.0f.xxxx;
        float2 sideRoughnessAo = 0.0f.xx;
        if (sideWeights.x > 0.0f)
        {
            const TerrainPlaneSample plane = TerrainSamplePlane(layer, float2(worldPosition.z, -worldPosition.y) / tiling, float2(dpdx.z, -dpdx.y) / tiling,
                                                                float2(dpdy.z, -dpdy.y) / tiling, tone.rgb, tone.a, detailWeight);
            const float2 tangent = plane.tangent * params.z;
            const float z = sqrt(saturate(1.0f - dot(tangent, tangent)));
            sideNormal += float3(z * macroNormal.x, macroNormal.y - tangent.y, macroNormal.z + tangent.x) * sideWeights.x;
            sideColor += plane.color * sideWeights.x;
            sideRoughnessAo += plane.roughnessAo * sideWeights.x;
        }
        if (sideWeights.y > 0.0f)
        {
            const TerrainPlaneSample plane = TerrainSamplePlane(layer, float2(worldPosition.x, -worldPosition.y) / tiling, float2(dpdx.x, -dpdx.y) / tiling,
                                                                float2(dpdy.x, -dpdy.y) / tiling, tone.rgb, tone.a, detailWeight);
            const float2 tangent = plane.tangent * params.z;
            const float z = sqrt(saturate(1.0f - dot(tangent, tangent)));
            sideNormal += float3(macroNormal.x + tangent.x, macroNormal.y - tangent.y, z * macroNormal.z) * sideWeights.y;
            sideColor += plane.color * sideWeights.y;
            sideRoughnessAo += plane.roughnessAo * sideWeights.y;
        }
        color += sideColor * cliff;
        normal += sideNormal * cliff;
        roughnessAo += sideRoughnessAo * cliff;
    }
    TerrainSurfaceSample result;
    result.albedo = color.rgb;
    result.height = color.a;
    result.normal = normal;
    result.roughness = roughnessAo.x;
    result.ao = roughnessAo.y;
    return result;
}

// 周りの 4 標本の制御図から、この画素で一番効く 2 材質を選ぶ。
void TerrainPickSurfaces(float2 tileUv, uint tileLayer, out uint first, out uint second, out float blend, out float wet)
{
    const float samples = gTerrainTileCells + 1.0f;
    const float2 texel = tileUv * samples - 0.5f;
    const float2 base = floor(texel);
    const float2 f = texel - base;
    const int last = int(samples) - 1;
    const int2 cell = int2(base);
    const float cornerWeights[4] = {(1.0f - f.x) * (1.0f - f.y), f.x * (1.0f - f.y), (1.0f - f.x) * f.y, f.x * f.y};
    uint ids[8];
    float weights[8];
    uint count = 0u;
    wet = 0.0f;
    for (uint corner = 0u; corner < 4u; ++corner)
    {
        const int2 at = min(cell + int2(corner & 1u, corner >> 1u), int2(last, last));
        const uint value = gTerrainControl.Load(int4(at, tileLayer, 0));
        wet += cornerWeights[corner] * float(value >> 28u) / 15.0f;
        const float overlay = float((value >> 10u) & 255u) / 255.0f;
        const uint materials[2] = {value & 31u, (value >> 5u) & 31u};
        const float amounts[2] = {cornerWeights[corner] * (1.0f - overlay), cornerWeights[corner] * overlay};
        for (uint part = 0u; part < 2u; ++part)
        {
            if (amounts[part] <= 0.0f)
                continue;
            uint index = 0u;
            while (index < count && ids[index] != materials[part])
                ++index;
            if (index == count)
            {
                ids[count] = materials[part];
                weights[count] = 0.0f;
                ++count;
            }
            weights[index] += amounts[part];
        }
    }
    first = 0u;
    second = 0u;
    float firstWeight = -1.0f;
    float secondWeight = 0.0f;
    for (uint index = 0u; index < count; ++index)
    {
        if (weights[index] > firstWeight)
        {
            second = first;
            secondWeight = max(firstWeight, 0.0f);
            first = ids[index];
            firstWeight = weights[index];
        }
        else if (weights[index] > secondWeight)
        {
            second = ids[index];
            secondWeight = weights[index];
        }
    }
    blend = secondWeight / max(firstWeight + secondWeight, 1.0e-4f);
}

// 画素の地表：2 材質を高さで混ぜる。albedo は色の図に掛ける濃淡。
TerrainSurfaceSample TerrainResolveSurface(TerrainVertex input, float3 macroNormal, float detailWeight, out float wet)
{
    uint first;
    uint second;
    float blend;
    TerrainPickSurfaces(input.tileUv, input.tileLayer, first, second, blend, wet);
    const uint limit = gTerrainSurfaceCount - 1u;
    first = min(first, limit);
    second = min(second, limit);
    // 44 度までは真上からの投影だけで読む。53 度より急なら側面だけ。
    const float cliff = 1.0f - smoothstep(0.6f, 0.72f, macroNormal.y);
    const float3 dpdx = ddx(input.worldPosition);
    const float3 dpdy = ddy(input.worldPosition);
    TerrainSurfaceSample a = TerrainSampleSurface(first, input.worldPosition, dpdx, dpdy, macroNormal, cliff, detailWeight);
    if (blend > 0.02f)
    {
        const TerrainSurfaceSample b = TerrainSampleSurface(second, input.worldPosition, dpdx, dpdy, macroNormal, cliff, detailWeight);
        // 遠くは高さの図の起伏が小さいので、幅を広げて境界の線が立たないようにする。
        const float contrast = max(gTerrainSurfaceParams.Load(int4(first, 0, 0, 0)).y, 0.01f) * lerp(4.0f, 1.0f, detailWeight);
        const float heightA = a.height + (1.0f - blend);
        const float heightB = b.height + blend;
        const float top = max(heightA, heightB) - contrast;
        const float weightA = max(heightA - top, 0.0f);
        const float weightB = max(heightB - top, 0.0f);
        const float t = weightB / max(weightA + weightB, 1.0e-4f);
        a.albedo = lerp(a.albedo, b.albedo, t);
        a.height = lerp(a.height, b.height, t);
        a.normal = lerp(a.normal, b.normal, t);
        a.roughness = lerp(a.roughness, b.roughness, t);
        a.ao = lerp(a.ao, b.ao, t);
    }
    a.normal = normalize(a.normal);
    return a;
}

// 色の図（線形の色と環境光の遮蔽）。瓦片の縁で標本が揃うよう、格子の角に合わせて読む。焼き上がるまでは中間の灰。
float4 TerrainReadColorMap(TerrainVertex input)
{
    if (gTerrainColorMapReady == 0u)
    {
        return float4(0.18f, 0.18f, 0.18f, 1.0f);
    }
    const float samples = gTerrainTileCells + 1.0f;
    const float2 unit = (input.tileUv * samples - 0.5f) / gTerrainTileCells;
    const float2 uv = (unit * (kColorMapSize - 1.0f) + 0.5f) / kColorMapSize;
    const float4 map = gTerrainColorMap.Sample(gLinearClamp, float3(uv, input.tileLayer));
    return float4(map.rgb * map.rgb, map.a);
}

// 色は色の図で決まる。近くは材質の濃淡・法線・粗さを載せ、遠くは色の図だけ。全体に積雪・湿りを掛ける。
TerrainSurfaceSample TerrainShadeSurface(TerrainVertex input, float3 macroNormal)
{
    const float distanceToCamera = length(input.worldPosition - gSurfaceCameraPosition);
    const float far = saturate((distanceToCamera - kFarStartMeters) / kFarFadeMeters);
    const float4 map = TerrainReadColorMap(input);
    TerrainSurfaceSample surface;
    float wet = gTerrainWetness;
    if (far >= 1.0f)
    {
        surface.albedo = map.rgb;
        surface.height = 0.5f;
        surface.normal = macroNormal;
        surface.roughness = kFarRoughness;
        surface.ao = map.a;
    }
    else
    {
        float sampleWet;
        const float detailWeight = 1.0f - smoothstep(kDetailStartMeters, kDetailEndMeters, distanceToCamera);
        surface = TerrainResolveSurface(input, macroNormal, detailWeight, sampleWet);
        wet = max(wet, sampleWet);
        surface.albedo = lerp(surface.albedo, 1.0f.xxx, far) * map.rgb;
        surface.normal = normalize(lerp(surface.normal, macroNormal, far));
        surface.roughness = lerp(surface.roughness, kFarRoughness, far);
        surface.ao = lerp(surface.ao, 1.0f, far) * map.a;
    }
    if (gTerrainSnowAmount > 0.0f)
    {
        const float snow = gTerrainSnowAmount * smoothstep(gTerrainSnowLine, gTerrainSnowLine + 30.0f, input.worldPosition.y) *
            smoothstep(0.6f, 0.85f, macroNormal.y);
        surface.albedo = lerp(surface.albedo, float3(0.86f, 0.88f, 0.92f), snow);
        surface.roughness = lerp(surface.roughness, 0.55f, snow);
        surface.normal = normalize(lerp(surface.normal, macroNormal, snow * 0.8f));
    }
    if (wet > 0.0f)
    {
        surface.albedo *= lerp(1.0f, 0.5f, wet);
        surface.roughness *= lerp(1.0f, 0.35f, wet);
    }
    return surface;
}

#endif
