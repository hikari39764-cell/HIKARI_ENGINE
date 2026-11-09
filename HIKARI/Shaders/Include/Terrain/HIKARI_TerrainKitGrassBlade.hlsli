#ifndef HIKARI_TERRAINKIT_GRASS_BLADE_INCLUDED
#define HIKARI_TERRAINKIT_GRASS_BLADE_INCLUDED

// 地形モジュールの草の一枚：乱数、風、組の光の入力、葉の形と色、頂点。Render3D_TerrainKitGrass.hlsl の資源を使う。

uint GrassHash(int2 cell)
{
    uint hash = asuint(cell.x) * 73856093u ^ asuint(cell.y) * 19349663u;
    hash = (hash ^ (hash >> 13u)) * 1274126177u;
    return hash ^ (hash >> 16u);
}

uint GrassNextHash(uint hash)
{
    hash = hash * 747796405u + 2891336453u;
    return ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
}

float GrassUnit(uint hash)
{
    return (float)(hash >> 8u) * (1.0f / 16777216.0f);
}

float2 GrassTileUv(float2 worldXZ, float2 tileOriginXZ)
{
    const float samples = gTerrainTileCells + 1.0f;
    return ((worldXZ - tileOriginXZ) / gTerrainTileSize * gTerrainTileCells + 0.5f) / samples;
}

float GrassTerrainHeight(float2 uv, uint layer)
{
    return gTerrainHeightMin + gTerrainHeightRange * gTerrainHeight.SampleLevel(gLinearClamp, float3(uv, layer), 0.0f);
}

float GrassValueNoise(float2 position)
{
    const int2 cell = (int2)floor(position);
    const float2 f = position - floor(position);
    const float2 s = f * f * (3.0f - 2.0f * f);
    const float a = GrassUnit(GrassHash(cell));
    const float b = GrassUnit(GrassHash(cell + int2(1, 0)));
    const float c = GrassUnit(GrassHash(cell + int2(0, 1)));
    const float d = GrassUnit(GrassHash(cell + int2(1, 1)));
    return lerp(lerp(a, b, s.x), lerp(c, d, s.x), s.y);
}

// 風：向きへ流れる大きな噪声（突風）と小さな噪声の和。0..1。
float GrassWindForce(float2 position)
{
    const float travel = gTimeParams.x * gGrassWindSpeed;
    const float gust = GrassValueNoise((position - gGrassWindDirection * travel) * 0.07f);
    const float ripple = GrassValueNoise((position - gGrassWindDirection * travel * 1.7f) * 0.31f);
    return saturate(gust * 0.75f + ripple * 0.45f - 0.1f);
}

GrassBlock GrassBuildBlock(float2 centerXZ, GrassPatch patch)
{
    GrassBlock block;
    const float2 local = clamp(centerXZ - patch.tileOriginXZ, 0.0f, gTerrainTileSize);
    const float2 uv = GrassTileUv(patch.tileOriginXZ + local, patch.tileOriginXZ);
    const float2 normalXZ = gTerrainNormal.SampleLevel(gLinearClamp, float3(uv, patch.tileLayer), 0.0f);
    block.groundNormal = normalize(float3(normalXZ.x, sqrt(saturate(1.0f - dot(normalXZ, normalXZ))), normalXZ.y));
    block.up = normalize(lerp(float3(0.0f, 1.0f, 0.0f), block.groundNormal, 0.35f));
    block.wind = GrassWindForce(centerXZ) * gGrassWindStrength;
    block.cameraHeight = GrassTerrainHeight(uv, patch.tileLayer) + 0.3f;
    block.ground = 0.0f.xxx;
    if (gGrassColorMapReady != 0u)
    {
        const float colorSize = 257.0f;
        const float2 unit = (uv * (gTerrainTileCells + 1.0f) - 0.5f) / gTerrainTileCells;
        const float3 map = gTerrainColorMap.SampleLevel(gLinearClamp,
            float3((unit * (colorSize - 1.0f) + 0.5f) / colorSize, patch.tileLayer), 0.0f).rgb;
        block.ground = map * map;
    }
    return block;
}

// 組で一度読む光の入力。探針は上向きの一方向（草の面はほぼ上を向く）。
GrassLighting GrassReadLighting(float3 position, float3 camera)
{
    GrassLighting lighting;
    lighting.light = normalize(-gDirectionalDir.xyz);
    const float3 up = float3(0.0f, 1.0f, 0.0f);
    const float3 view = normalize(camera - position);
    lighting.view = view;
    lighting.halfway = normalize(lighting.light + view);
    float sky = 1.0f;
    const float4 probe = HikariResolveLightProbeDiffuse(position, up, up, view, sky);
    lighting.probes = probe.rgb;
    if (probe.a < 1.0f)
    {
        float3 skyDiffuse = gAmbientColor.rgb * gAmbientIntensity;
        if (gIblHasIrradiance > 0.5f)
        {
            const float3 irradiance = gIblIrradianceTex.SampleLevel(gLinearWrap, HikariSkyEnvironmentDirection(up), 0.0f).rgb;
            skyDiffuse += HikariApplySkyRadianceScale(irradiance, gSkyTint, gSkyRadianceScale) * max(gSkyLightIntensity, 0.0f);
        }
        lighting.probes += skyDiffuse * (1.0f - probe.a);
    }
    const float cloud = HikariSampleCloudSolarVisibility(gMaterialTexturePool[HikariSolarVisibilityDescriptor(gCloudSolarVisibility)],
        gCloudSolarVisibility, position);
    lighting.sun = gDirectionalColor.rgb * (gDirectionalIntensity * cloud);
    // 透過：葉の向きに依らない分と、太陽の方を見ると強くなる分（法線で決めると、法線を上へ寄せる遠くだけ暗くなる）。
    lighting.toSunGlow = 0.35f + 0.65f * pow(saturate(dot(-view, lighting.light) * 0.5f + 0.5f), 4.0f);
    return lighting;
}

// 一か所の光。反射は光沢の強い Blinn の近似で、遠くは粗くして、きらつきを止める。
void GrassShadePoint(GrassLighting lighting, float3 albedo, float3 normal, float specularPower, float specularScale,
    float through, float occlusion, out float3 sunLit, out float3 ambient)
{
    normal = dot(normal, lighting.view) < 0.0f ? -normal : normal;
    const float facing = dot(normal, lighting.light);
    // 葉は薄く向きがばらばらなので、向きで明暗を強く分けない（塊ごとの濃淡が斑に見える）。
    const float wrapped = saturate((facing + 0.7f) / 1.7f);
    const float specular = specularScale * pow(saturate(dot(normal, lighting.halfway)), specularPower) * saturate(facing);
    sunLit = lighting.sun * (albedo * ((wrapped * lerp(0.75f, 1.0f, occlusion) + through) * 0.3183f) + specular);
    ambient = albedo * lighting.probes * occlusion;
}

// 制御図の 4 標本から距離で重みを付けて一つ選び、主材質と重ね材質を混合度で選ぶ（図の解像度が見えない抖動の境目）。
bool GrassSelectSurface(float2 uv, uint layer, uint hash, out uint surface)
{
    const float samples = gTerrainTileCells + 1.0f;
    const uint4 gathered = gTerrainControl.GatherRed(gPointClamp, float3(uv, layer));
    const float2 fraction = frac(uv * samples - 0.5f);
    const uint a = GrassNextHash(hash);
    const bool right = GrassUnit(hash) < fraction.x;
    const bool lower = GrassUnit(a) < fraction.y;
    const uint control = lower ? (right ? gathered.y : gathered.x) : (right ? gathered.z : gathered.w);
    const float blend = (float)((control >> 10u) & 255u) / 255.0f;
    const bool overlay = GrassUnit(GrassNextHash(a)) < blend;
    surface = overlay ? ((control >> 5u) & 31u) : (control & 31u);
    return ((control >> 25u) & 1u) == 0u && surface < gGrassSurfaceCount;
}

// 位置は世界の格子だけで決まり、遠近の段を移っても同じ葉は同じ所にある。残る段（格子番号の末尾の 0 の数）が高いほど
// 揺らぎを広げ、遠くでも格子に見えないようにする。
float2 GrassBladePosition(int2 baseCell, out uint survives, out uint hash)
{
    const uint bits = asuint(baseCell.x) | asuint(baseCell.y);
    survives = bits == 0u ? GRASS_TOP_LEVEL : min(firstbitlow(bits), GRASS_TOP_LEVEL);
    const float cell = GRASS_BASE_SPACING * (float)(1u << survives);
    hash = GrassHash(baseCell);
    float2 position = ((float2)baseCell + 0.5f) * GRASS_BASE_SPACING;
    position.x += (GrassUnit(hash) - 0.5f) * cell;
    hash = GrassNextHash(hash);
    position.y += (GrassUnit(hash) - 0.5f) * cell;
    return position;
}

// 葉があるか（出力の数を先に決めるための軽い判定）。消える段に近づいた葉は背を低くして消すので、消えきったら無し。
bool GrassBladeExists(GrassPatch patch, int2 baseCell, float2 cameraXZ, out uint surface)
{
    surface = 0u;
    uint survives, hash;
    const float2 position = GrassBladePosition(baseCell, survives, hash);
    const float offsetLength = length(position - cameraXZ);
    if (offsetLength >= gGrassFirstRange * (float)(1u << survives))
        return false;
    const float2 tileLocal = position - patch.tileOriginXZ;
    if (any(tileLocal < 0.0f) || any(tileLocal >= gTerrainTileSize))
        return false;
    hash = GrassNextHash(hash);
    if (!GrassSelectSurface(GrassTileUv(position, patch.tileOriginXZ), patch.tileLayer, hash, surface))
        return false;
    hash = GrassNextHash(hash);
    const float density = gTerrainSurfaceParams.Load(int4(surface, GRASS_PARAM_SHAPE, 0, 0)).x * gGrassDensity;
    return GrassUnit(hash) < (survives == 0u ? density * GRASS_FINEST_DENSITY : density);
}

GrassBlade GrassBuildBlade(GrassPatch patch, GrassBlock block, GrassLighting lighting, GrassParams params, int2 baseCell,
    float3 camera)
{
    GrassBlade blade = (GrassBlade)0;
    uint survives, hash;
    float2 position = GrassBladePosition(baseCell, survives, hash);
    const float distanceToCamera = length(position - camera.xz);
    const float fadeEnd = gGrassFirstRange * (float)(1u << survives);
    const float fade = saturate((fadeEnd - distanceToCamera) / (fadeEnd * 0.3f));

    // 塊（Voronoi）：近い 4 個の揺らした点のうち一番近い点の塊に属する。
    const float clumpSize = max(params.pose.w, 0.05f);
    const int2 clumpCell = (int2)floor(position / clumpSize - 0.5f);
    float clumpBest = 1.0e9f;
    float2 clumpCenter = position;
    uint clumpHash = 0u;
    for (int index = 0; index < 4; ++index)
    {
        const int2 candidate = clumpCell + int2(index & 1, index >> 1);
        const uint candidateHash = GrassHash(candidate * int2(31, 17) + int2(7, 3));
        const float2 site = ((float2)candidate + float2(GrassUnit(candidateHash),
            GrassUnit(GrassNextHash(candidateHash)))) * clumpSize;
        const float2 offset = site - position;
        const float distanceSquared = dot(offset, offset);
        if (distanceSquared < clumpBest)
        {
            clumpBest = distanceSquared;
            clumpCenter = site;
            clumpHash = candidateHash;
        }
    }
    const float clumpRandom = GrassUnit(GrassNextHash(clumpHash ^ 0x51ed270bu));
    hash = GrassNextHash(hash ^ 0x3c6ef372u);
    position = lerp(position, clumpCenter, params.clump.y * GrassUnit(hash));
    const float2 local = clamp(position - patch.tileOriginXZ, 0.0f, gTerrainTileSize);
    const float2 uv = GrassTileUv(patch.tileOriginXZ + local, patch.tileOriginXZ);

    hash = GrassNextHash(hash);
    const float centerShare = 1.0f - saturate(sqrt(clumpBest) / clumpSize);
    float height = lerp(params.shape.y, params.shape.z, GrassUnit(hash));
    height *= 1.0f + params.clump.x * (clumpRandom * 2.0f - 1.0f + centerShare * 0.5f);
    // 穂をつける葉は周りより高く伸びる。
    blade.plume = GrassUnit(GrassNextHash(hash ^ 0x6a09e667u)) < params.plume.a ? 1.0f : 0.0f;
    height *= 1.0f + blade.plume * 0.45f;
    height *= fade * smoothstep(0.55f, 0.72f, block.groundNormal.y);
    const float nearCamera = 1.0f - smoothstep(0.6f, 2.8f, length(float3(position.x, block.cameraHeight, position.y) - camera));
    height *= 1.0f - 0.45f * nearCamera;

    const float wind = block.wind * params.extra.z * (0.8f + 0.4f * GrassUnit(hash ^ 0x7f4a7c15u));
    // 向き：葉ごとの乱数、塊で揃った向き、塊の中心から離れる向き、風下を混ぜる。
    hash = GrassNextHash(hash);
    const float randomAngle = GrassUnit(hash) * 6.28318530718f;
    const float clumpAngle = GrassUnit(GrassNextHash(clumpHash)) * 6.28318530718f;
    const float2 clumpFacing = float2(cos(clumpAngle), sin(clumpAngle));
    float2 facing = lerp(float2(cos(randomAngle), sin(randomAngle)), clumpFacing, params.clump.z);
    const float2 away = position - clumpCenter;
    facing += away * rsqrt(max(dot(away, away), 1.0e-6f)) * params.clump.w;
    facing += gGrassWindDirection * wind * 1.2f;
    const float2 fromCamera = position - camera.xz;
    facing += fromCamera * rsqrt(max(dot(fromCamera, fromCamera), 1.0e-6f)) * nearCamera * 3.0f;
    facing *= rsqrt(max(dot(facing, facing), 1.0e-6f));

    hash = GrassNextHash(hash);
    const float tilt = params.pose.x + params.pose.y * (GrassUnit(hash) * 2.0f - 1.0f) + wind * 0.55f + nearCamera * 0.7f;
    const float bend = params.pose.z + wind * 0.2f;
    blade.root = float3(position.x, GrassTerrainHeight(uv, patch.tileLayer), position.y);
    // 根元はまっすぐ立ち上がり、先端が倒れる三次ベジェ。曲がりは途中の点を持ち上げて先を垂らす。
    blade.lean = float3(facing.x, 0.0f, facing.y);
    const float rise = cos(tilt) * height;
    const float reach = sin(tilt) * height;
    blade.p3 = blade.root + block.up * rise + blade.lean * reach;
    blade.p1 = blade.root + block.up * rise * 0.34f + blade.lean * reach * 0.06f;
    blade.p2 = blade.root + block.up * (rise * 0.72f + bend * height * 0.6f) + blade.lean * reach * 0.42f;
    blade.side = normalize(cross(block.up, blade.lean));
    // 遠くでは残った葉が消えた葉の分も覆うよう、距離に応じて幅を広げる。
    blade.width = params.shape.w * lerp(0.75f, 1.25f, GrassUnit(GrassNextHash(hash))) *
        max(distanceToCamera / gGrassFirstRange, 1.0f) * saturate(fade * 2.0f);

    // 葉が視線に対して横向きの時、視空間で少し太らせる（細い三角形を減らし、草原を詰まって見せる）。
    const float3 chord = blade.p3 - blade.root;
    const float3 bladeNormal = cross(blade.side, chord);
    const float edgeOn = 1.0f - abs(dot(bladeNormal, lighting.view)) * rsqrt(max(dot(bladeNormal, bladeNormal), 1.0e-8f));
    blade.thicken = 1.6f * edgeOn * edgeOn * edgeOn * smoothstep(2.0f, 12.0f, distanceToCamera);
    blade.screenSide = normalize(cross(lighting.view, chord));

    const float highEnd = gGrassFirstRange * (float)(1u << GRASS_LAST_HIGH_LEVEL);
    blade.morph = smoothstep(highEnd * 0.6f, highEnd, distanceToCamera);
    blade.phase = GrassUnit(clumpHash ^ hash) * 6.28318530718f;
    blade.bobAmount = height * (0.03f + 0.09f * wind);
    blade.sideLight = dot(blade.side, lighting.light) * 0.3f;
    blade.fog = gFogParams.x < 0.5f ? 0.0f : HikariForwardFogFactor(blade.root);

    // 色：根元は地面の色へ寄せ、先端の色は塊ごとにわずかに振る（絵のような色面を保つ）。
    const float3 ground = gGrassColorMapReady != 0u ? block.ground : params.root.rgb;
    const float tint = 1.0f + params.extra.x * (clumpRandom * 2.0f - 1.0f);
    const float bladeTint = lerp(0.94f, 1.06f, GrassUnit(GrassNextHash(hash ^ 0x2f6e2b1u)));
    const float3 rootColor = lerp(params.root.rgb, ground, params.extra.y) * bladeTint;
    const float3 tipColor = blade.plume > 0.0f ? params.plume.rgb * bladeTint : params.tip.rgb * tint * bladeTint;
    // 遠くでは法線を塊の共通の向きへ寄せ、粗くする。根元は立ち上がりの法線、先端は倒れた先の法線。
    const float far = smoothstep(gGrassFirstRange * 0.6f, gGrassFirstRange * 3.0f, distanceToCamera);
    const float3 farNormal = block.groundNormal + float3(clumpFacing.x, 0.0f, clumpFacing.y) * 0.45f;
    const float3 normalRoot = normalize(lerp(cross(blade.side, blade.p1 - blade.root) * 8.0f / max(height, 0.02f), farNormal, far));
    const float3 normalTip = normalize(lerp(cross(blade.side, blade.p3 - blade.p2) * 4.0f / max(height, 0.02f), farNormal, far));
    const float roughness = lerp(params.tip.a, min(params.tip.a + 0.3f, 1.0f), far);
    const float roughnessSquared = roughness * roughness;
    const float power = clamp(2.0f / max(roughnessSquared * roughnessSquared, 1.0e-3f) - 2.0f, 1.0f, 512.0f);
    const float specularScale = 0.04f * (power + 8.0f) * 0.0398f;
    const float through = params.root.a * lighting.toSunGlow;
    GrassShadePoint(lighting, rootColor, normalRoot, power, specularScale, through * 0.45f,
        lerp(params.extra.w, 1.0f, far * 0.35f), blade.sunLitRoot, blade.ambientRoot);
    GrassShadePoint(lighting, tipColor, normalTip, power, specularScale, through, 1.0f, blade.sunLitTip, blade.ambientTip);
    return blade;
}

float3 GrassCurve(GrassBlade blade, float t)
{
    const float i = 1.0f - t;
    return blade.root * (i * i * i) + blade.p1 * (3.0f * i * i * t) + blade.p2 * (3.0f * i * t * t) + blade.p3 * (t * t * t);
}

GrassVertex GrassBuildVertex(GrassBlade blade, uint vertexIndex, uint pairs)
{
    const bool high = pairs == GRASS_HIGH_PAIRS;
    const bool tip = vertexIndex == pairs * 2u;
    const uint row = tip ? pairs : vertexIndex / 2u;
    const float across = tip ? 0.0f : ((vertexIndex & 1u) != 0u ? 1.0f : -1.0f);
    const float t = high ? GRASS_HIGH_T[row] : GRASS_LOW_T[row];

    float3 center = GrassCurve(blade, t);
    float widthShare = high ? GRASS_HIGH_WIDTH[row] : GRASS_LOW_WIDTH[row];
    if (high && blade.morph > 0.0f)
    {
        // 低い段の折れ線の上へ寄せておき、段を移っても形が跳ねないようにする。
        const float lowRow = (float)row / (float)GRASS_HIGH_PAIRS * (float)GRASS_LOW_PAIRS;
        const uint lowFloor = min((uint)lowRow, GRASS_LOW_PAIRS - 1u);
        const float share = lowRow - (float)lowFloor;
        center = lerp(center, lerp(GrassCurve(blade, GRASS_LOW_T[lowFloor]), GrassCurve(blade, GRASS_LOW_T[lowFloor + 1u]), share),
            blade.morph);
        widthShare = lerp(widthShare, lerp(GRASS_LOW_WIDTH[lowFloor], GRASS_LOW_WIDTH[lowFloor + 1u], share), blade.morph);
    }

    // 上下の揺れ：葉ごとと葉の上の位置で位相をずらす。
    const float sway = sin(gTimeParams.x * 2.6f + blade.phase + t * 2.2f);
    center += (blade.lean * 0.8f + float3(0.0f, 0.35f, 0.0f)) * (sway * blade.bobAmount * t * t);

    if (blade.plume > 0.0f)
    {
        widthShare *= 1.0f + 1.4f * smoothstep(0.55f, 0.8f, t);
    }
    const float halfWidth = blade.width * widthShare * across;
    const float3 world = center + blade.side * halfWidth + blade.screenSide * (halfWidth * blade.thicken);

    GrassVertex output;
    output.position = mul(gSurfaceViewProj, float4(world, 1.0f));
    output.across = across;
    output.fog = blade.fog;
    const float share = blade.plume > 0.0f ? smoothstep(0.5f, 0.72f, t) : smoothstep(0.0f, 0.75f, t);
    output.sunLit = lerp(blade.sunLitRoot, blade.sunLitTip, share) * (1.0f + across * blade.sideLight);
    output.ambient = lerp(blade.ambientRoot, blade.ambientTip, share);
    return output;
}

#endif
