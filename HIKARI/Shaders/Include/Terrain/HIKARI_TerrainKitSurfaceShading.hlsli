#ifndef HIKARI_TERRAINKIT_SURFACE_SHADING_INCLUDED
#define HIKARI_TERRAINKIT_SURFACE_SHADING_INCLUDED

// 地形モジュールの地表の色。Render3D_TerrainKitSurface.hlsl の資源と TerrainVertex を使う。
// ---- 地表材質 ----
// 去重復：Mikkelsen 2022「Practical Real-Time Hex-Tiling」の六角タイル（3 標本を高さで鋭く混ぜる）。
// 崖：Quilez の biplanar の考え方で、主な側面 2 つだけを読む。法線は whiteout（Golus）で組む。
static const float kFarStartMeters = 240.0f;
static const float kFarFadeMeters = 80.0f;
static const float kFarCacheSize = 129.0f;

struct TerrainSurfaceSample
{
    float3 albedo;
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

float TerrainValueNoise(float2 position)
{
    const float2 base = floor(position);
    const float2 f = position - base;
    const float2 u = f * f * (3.0f - 2.0f * f);
    const int2 cell = int2(base);
    return lerp(lerp(TerrainHash2(cell).x, TerrainHash2(cell + int2(1, 0)).x, u.x),
                lerp(TerrainHash2(cell + int2(0, 1)).x, TerrainHash2(cell + int2(1, 1)).x, u.x), u.y);
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

TerrainSurfaceSample TerrainSampleSurface(uint surface, float3 worldPosition, float3 dpdx, float3 dpdy, float3 macroNormal, float cliff)
{
    const float4 params = gTerrainSurfaceParams.Load(int4(surface, 0, 0, 0));
    const float tiling = max(params.x, 0.01f);
    const float layer = (float)surface;
    float4 color = 0.0f.xxxx;
    float3 normal = 0.0f.xxx;
    float roughness = 0.0f;
    float ao = 0.0f;
    if (cliff < 1.0f)
    {
        float4 topColor;
        float4 topDetail;
        TerrainSamplePlanar(layer, worldPosition.xz / tiling, dpdx.xz / tiling, dpdy.xz / tiling, topColor, topDetail);
        const float weight = 1.0f - cliff;
        color += topColor * weight;
        normal += TerrainWhiteoutTop(topDetail.xy * 2.0f - 1.0f, params.z, macroNormal) * weight;
        roughness += topDetail.z * weight;
        ao += topDetail.w * weight;
    }
    if (cliff > 0.0f)
    {
        // 主な側面 2 つ（x 向きと z 向き）を面の向きで混ぜる。図の v は世界の下向き。
        float2 sideWeights = pow(abs(macroNormal.xz), 4.0f);
        sideWeights /= max(sideWeights.x + sideWeights.y, 1.0e-4f);
        float3 sideNormal = 0.0f.xxx;
        float4 sideColor = 0.0f.xxxx;
        float2 sideDetail = 0.0f.xx;
        if (sideWeights.x > 0.02f)
        {
            float4 planeColor;
            float4 sampled;
            TerrainSamplePlanar(layer, float2(worldPosition.z, -worldPosition.y) / tiling, float2(dpdx.z, -dpdx.y) / tiling,
                                float2(dpdy.z, -dpdy.y) / tiling, planeColor, sampled);
            const float2 tangent = (sampled.xy * 2.0f - 1.0f) * params.z;
            const float z = sqrt(saturate(1.0f - dot(tangent, tangent)));
            sideNormal += float3(z * macroNormal.x, macroNormal.y - tangent.y, macroNormal.z + tangent.x) * sideWeights.x;
            sideColor += planeColor * sideWeights.x;
            sideDetail += sampled.zw * sideWeights.x;
        }
        if (sideWeights.y > 0.02f)
        {
            float4 planeColor;
            float4 sampled;
            TerrainSamplePlanar(layer, float2(worldPosition.x, -worldPosition.y) / tiling, float2(dpdx.x, -dpdx.y) / tiling,
                                float2(dpdy.x, -dpdy.y) / tiling, planeColor, sampled);
            const float2 tangent = (sampled.xy * 2.0f - 1.0f) * params.z;
            const float z = sqrt(saturate(1.0f - dot(tangent, tangent)));
            sideNormal += float3(macroNormal.x + tangent.x, macroNormal.y - tangent.y, z * macroNormal.z) * sideWeights.y;
            sideColor += planeColor * sideWeights.y;
            sideDetail += sampled.zw * sideWeights.y;
        }
        color += sideColor * cliff;
        normal += sideNormal * cliff;
        roughness += sideDetail.x * cliff;
        ao += sideDetail.y * cliff;
    }
    TerrainSurfaceSample result;
    result.albedo = color.rgb;
    result.height = color.a;
    result.normal = normal;
    result.roughness = roughness;
    result.ao = ao;
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

// 画素の地表：2 材質を高さで混ぜ、広い明暗のむらを足す。
TerrainSurfaceSample TerrainResolveSurface(TerrainVertex input, float3 macroNormal, out float wet)
{
    uint first;
    uint second;
    float blend;
    TerrainPickSurfaces(input.tileUv, input.tileLayer, first, second, blend, wet);
    const uint limit = gTerrainSurfaceCount - 1u;
    first = min(first, limit);
    second = min(second, limit);
    const float cliff = 1.0f - smoothstep(0.55f, 0.75f, macroNormal.y);
    const float3 dpdx = ddx(input.worldPosition);
    const float3 dpdy = ddy(input.worldPosition);
    TerrainSurfaceSample a = TerrainSampleSurface(first, input.worldPosition, dpdx, dpdy, macroNormal, cliff);
    if (blend > 0.004f)
    {
        const TerrainSurfaceSample b = TerrainSampleSurface(second, input.worldPosition, dpdx, dpdy, macroNormal, cliff);
        const float contrast = max(gTerrainSurfaceParams.Load(int4(first, 0, 0, 0)).y, 0.01f);
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

// 遠景の色。瓦片の縁で標本が揃うよう、格子の角に合わせて読む。
TerrainSurfaceSample TerrainFarSurface(TerrainVertex input, float3 macroNormal)
{
    const float samples = gTerrainTileCells + 1.0f;
    const float2 unit = (input.tileUv * samples - 0.5f) / gTerrainTileCells;
    const float2 uv = (unit * (kFarCacheSize - 1.0f) + 0.5f) / kFarCacheSize;
    const float4 color = gTerrainFarCache.Sample(gLinearClamp, float3(uv, input.tileLayer));
    TerrainSurfaceSample result;
    result.albedo = color.rgb * color.rgb;
    result.height = 0.5f;
    result.normal = macroNormal;
    result.roughness = color.a;
    result.ao = 1.0f;
    return result;
}

// 近くは材質、遠くは遠景の色。全体に広い明暗のむらと積雪・湿りを掛ける。
TerrainSurfaceSample TerrainShadeSurface(TerrainVertex input, float3 macroNormal)
{
    const float distanceToCamera = length(input.worldPosition - gSurfaceCameraPosition);
    const float far = gTerrainFarReady != 0u ? saturate((distanceToCamera - kFarStartMeters) / kFarFadeMeters) : 0.0f;
    TerrainSurfaceSample surface;
    float wet = gTerrainWetness;
    if (far >= 1.0f)
    {
        surface = TerrainFarSurface(input, macroNormal);
    }
    else
    {
        float sampleWet;
        surface = TerrainResolveSurface(input, macroNormal, sampleWet);
        wet = max(wet, sampleWet);
        if (far > 0.0f)
        {
            const TerrainSurfaceSample distant = TerrainFarSurface(input, macroNormal);
            surface.albedo = lerp(surface.albedo, distant.albedo, far);
            surface.normal = normalize(lerp(surface.normal, distant.normal, far));
            surface.roughness = lerp(surface.roughness, distant.roughness, far);
            surface.ao = lerp(surface.ao, 1.0f, far);
        }
    }
    const float2 wide = input.worldPosition.xz;
    const float macro = TerrainValueNoise(wide * 0.019f) * 0.6f + TerrainValueNoise(wide * 0.083f) * 0.4f;
    surface.albedo *= lerp(0.82f, 1.14f, macro);
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
