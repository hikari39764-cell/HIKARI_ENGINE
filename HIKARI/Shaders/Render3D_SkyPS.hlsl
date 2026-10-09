cbuffer SkyCB : register(b0)
{
    float4x4 gSkyInverseViewProj;
    float4 gSkyScaleYaw;            // x radiance scale, y yaw, z pre-exposure, w below horizon (0 haze, 1 ground)
    float4 gSkyTintMode;            // rgb tint, a SkyMode
    float4 gSkyDisk;                // xyz toward the primary light, w angular radius [rad]
    float4 gSkyDiskLuminance;       // rgb average disk luminance incl. transmittance [nits]
    float4 gSkyMoonPhase;           // xyz toward sun, w primary disk is the moon
    float4 gSkyStars;               // x star intensity, y limiting magnitude, z night background [nits], w time [s]
    float4 gSkyCelestialX;          // celestial -> world columns
    float4 gSkyCelestialY;
    float4 gSkyCelestialZ;
};

cbuffer AtmosphereCB : register(b1)
{
    float4 gAtmospherePlanetParams;       // radius, top radius, Rayleigh H, Mie H
    float4 gAtmosphereRayleigh;           // scattering RGB, sun angular radius
    float4 gAtmosphereMie;                // scattering RGB, anisotropy
    float4 gAtmosphereOzone;              // absorption RGB, center height
    float4 gAtmosphereOzoneParams;        // width, aerial, observer sun T, albedo
    float4 gAtmosphereSunDirection;       // authoritative direction, enabled/LUT flag
    float4 gAtmosphereSunColorRadiance;   // source color, source radiance
    float4 gAtmosphereGroundColor;
};

Texture2D gSkyTex2D : register(t0);
TextureCube gSkyCube : register(t1);
Texture2D gAtmosphereSkyView : register(t2);
SamplerState gLinearClamp : register(s0);

#include "Include/Atmosphere/HIKARI_AtmosphereScattering.hlsli"
#include "Include/Sky/HIKARI_EquirectangularSampling.hlsli"
#include "Include/Sky/HIKARI_NightSky.hlsli"
#include "Include/Sky/HIKARI_SkyRadiance.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

float3 ReconstructWorldRay(float2 ndc)
{
    float4 worldNear = mul(
        gSkyInverseViewProj,
        float4(ndc, 0.0f, 1.0f));
    float4 worldFar = mul(
        gSkyInverseViewProj,
        float4(ndc, 1.0f, 1.0f));
    if (abs(worldNear.w) <= 1.0e-6f ||
        abs(worldFar.w) <= 1.0e-6f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }

    float3 direction = worldFar.xyz / worldFar.w -
        worldNear.xyz / worldNear.w;
    float lengthSquared = dot(direction, direction);
    if (!all(isfinite(direction)) || lengthSquared <= 1.0e-8f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    return direction * rsqrt(lengthSquared);
}

// 天空 LUT が無い時の空。カメラの高さを持たないので、背景の地面を見下ろす最低の高さから見る。
float3 EvaluateAtmosphereSky(float3 dir)
{
    const float3 observer = float3(
        0.0f,
        HikariAtmospherePlanetRadius() + HIKARI_ATMOSPHERE_BACKGROUND_HEIGHT,
        0.0f);
    return HikariAtmosphereEvaluateSkyRadiance(
        observer,
        dir,
        gAtmosphereGroundColor.rgb,
        gAtmosphereOzoneParams.w,
        gAtmosphereOzoneParams.y);
}

uint HashStarCell(uint3 cell)
{
    uint state = cell.x * 73856093u ^ cell.y * 19349663u ^ cell.z * 83492791u;
    state = state * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float4 StarCellRandom(uint3 cell)
{
    const uint a = HashStarCell(cell);
    const uint b = HashStarCell(uint3(a, cell.z + 17u, cell.x ^ 0x9e37u));
    return float4(a & 0xffffu, a >> 16u, b & 0xffffu, b >> 16u) / 65535.0f;
}

// 星の色。肉眼で見える星の色温度の分布（赤い M 型から青白い B 型まで、黄から白が多い）に沿って選び、輝度 1 にする。
float3 StarColor(float u)
{
    static const float3 kM = float3(1.0f, 0.62f, 0.36f);
    static const float3 kK = float3(1.0f, 0.78f, 0.55f);
    static const float3 kG = float3(1.0f, 0.93f, 0.82f);
    static const float3 kA = float3(0.82f, 0.88f, 1.0f);
    static const float3 kB = float3(0.62f, 0.72f, 1.0f);
    float3 color;
    if (u < 0.12f)
        color = lerp(kM, kK, u / 0.12f);
    else if (u < 0.45f)
        color = lerp(kK, kG, (u - 0.12f) / 0.33f);
    else if (u < 0.82f)
        color = lerp(kG, kA, (u - 0.45f) / 0.37f);
    else
        color = lerp(kA, kB, (u - 0.82f) / 0.18f);
    return color / dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float MilkyWayHash(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float MilkyWayValueNoise(float3 p)
{
    const float3 cell = floor(p);
    const float3 f = frac(p);
    const float3 w = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(MilkyWayHash(cell), MilkyWayHash(cell + float3(1, 0, 0)), w.x),
                    lerp(MilkyWayHash(cell + float3(0, 1, 0)), MilkyWayHash(cell + float3(1, 1, 0)), w.x), w.y),
                lerp(lerp(MilkyWayHash(cell + float3(0, 0, 1)), MilkyWayHash(cell + float3(1, 0, 1)), w.x),
                    lerp(MilkyWayHash(cell + float3(0, 1, 1)), MilkyWayHash(cell + float3(1, 1, 1)), w.x), w.y),
                w.z);
}

float MilkyWayFbm(float3 p)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    [unroll] for (uint octave = 0u; octave < 4u; ++octave)
    {
        sum += amplitude * MilkyWayValueNoise(p);
        p = p * 2.03f + 7.1f;
        amplitude *= 0.5f;
    }
    return sum;
}

// 銀河。赤道座標（J2000）の天球方向を銀河座標へ回し、銀河面に沿った帯の面輝度を与える。
// 中心（いて座、l = 0）付近が最も明るく厚く（約 20.5 等/平方秒角）、反中心へ向けて暗くなる。
// 星雲の塊と、銀河面に沿った暗い塵の帯（はくちょう座からいて座の暗黒帯）を雑音で付ける。
float3 EvaluateMilkyWay(float3 celestial)
{
    static const float3 kGalacticCenter = float3(-0.05488f, -0.87343f, -0.48392f);
    static const float3 kGalacticL90 = float3(0.49413f, -0.44491f, 0.74699f);
    static const float3 kGalacticNorthPole = float3(-0.86768f, -0.19808f, 0.45599f);
    const float3 g = float3(dot(celestial, kGalacticCenter), dot(celestial, kGalacticL90),
        dot(celestial, kGalacticNorthPole));
    const float latitude = asin(clamp(g.z, -1.0f, 1.0f)) * 57.29578f;
    const float longitude = atan2(g.y, g.x) * 57.29578f;
    const float toward = exp(-(longitude * longitude) / (2.0f * 45.0f * 45.0f));
    const float width = 5.0f + 7.0f * toward;
    const float disk = exp(-abs(latitude) / width) * (0.35f + 0.65f * toward);
    const float bulge = 0.8f * exp(-(longitude * longitude + latitude * latitude * 2.0f) / (2.0f * 11.0f * 11.0f));
    if (disk + bulge < 0.01f)
    {
        return 0.0f.xxx;
    }
    // 雑音の平均はおよそ 0.5。構造の平均が 1 前後になるよう塊と細部を重ねる。
    const float clumps = MilkyWayFbm(celestial * 9.0f);
    const float fine = MilkyWayFbm(celestial * 31.0f + 3.7f);
    // 暗黒帯は銀経 -10 から 80 度で銀河面のすぐそばに出る。帯の明るさの一部だけを削る。
    const float riftRange = smoothstep(-20.0f, -5.0f, longitude) * (1.0f - smoothstep(70.0f, 90.0f, longitude));
    const float rift = riftRange * exp(-(latitude * latitude) / (2.0f * 2.2f * 2.2f)) * smoothstep(0.4f, 0.7f, clumps);
    const float structure = (0.3f + 1.4f * clumps) * (0.7f + 0.6f * fine) * (1.0f - 0.5f * rift);
    // いて座の星雲付近でおよそ 20 等/平方秒角。
    static const float kPeakLuminance = 6.0e-4f;
    const float3 color = lerp(float3(0.82f, 0.88f, 1.0f), float3(1.0f, 0.90f, 0.78f), saturate(toward + bulge));
    return color / dot(color, float3(0.2126f, 0.7152f, 0.0722f)) * ((disk + bulge) * structure * kPeakLuminance);
}

// 星空。天球に固定した立方体格子の各セルに確率 kDensity で星を一つ置く。星の数は実際の分布
// log10 N(<m) = 0.45 (m - 6) + 3.72（全天、m 等より明るい星の数）に合わせ、限界等級までの星数から格子の細かさを決める。
// 各星は照度 2.54e-6 * 10^(-0.4 m) lux の点光源で、画素程度のぼかしに照度を保って広げる。
float3 EvaluateStars(float3 worldDir)
{
    const float intensity = gSkyStars.x;
    if (intensity <= 0.0f)
    {
        return 0.0f.xxx;
    }
    // 画素ごとに分岐する前に画面微分を取る（2x2 の組で一部だけ抜けると微分が壊れる）。
    const float3 c = float3(dot(worldDir, gSkyCelestialX.xyz), dot(worldDir, gSkyCelestialY.xyz),
        dot(worldDir, gSkyCelestialZ.xyz));
    const float3 a = abs(c);
    float2 uv;
    uint face;
    if (a.x >= a.y && a.x >= a.z)
    {
        uv = c.yz / a.x;
        face = c.x > 0.0f ? 0u : 1u;
    }
    else if (a.y >= a.z)
    {
        uv = c.xz / a.y;
        face = c.y > 0.0f ? 2u : 3u;
    }
    else
    {
        uv = c.xy / a.z;
        face = c.z > 0.0f ? 4u : 5u;
    }
    static const float kDensity = 0.35f;
    const float limitingMagnitude = gSkyStars.y;
    const float starCount = pow(10.0f, 0.45f * (limitingMagnitude - 6.0f) + 3.72f);
    const float cells = max(4.0f, floor(sqrt(starCount / (6.0f * kDensity))));
    const float2 grid = (uv * 0.5f + 0.5f) * cells;
    // セル一つの立体角。立方体面の面積要素 du dv / (1 + u^2 + v^2)^1.5。
    const float cellSolidAngle = 4.0f / (cells * cells) * pow(1.0f + dot(uv, uv), -1.5f);
    // 星の像は画面上で丸く描く。格子座標の画素あたりの変化（ヤコビアン）で格子のずれを画素のずれへ戻す。
    const float2 gridDx = ddx(grid);
    const float2 gridDy = ddy(grid);
    const float gridDeterminant = gridDx.x * gridDy.y - gridDy.x * gridDx.y;
    const float cellsPerPixel = max(max(length(gridDx), length(gridDy)), 1.0e-6f);
    // 面の境目などで微分が退化しても星が極端に明るくならないよう、伸びを 20 倍までに抑える。
    const float gridAreaPerPixel = max(abs(gridDeterminant), 0.05f * cellsPerPixel * cellsPerPixel);
    if (worldDir.y <= -0.02f)
    {
        return 0.0f.xxx;
    }
    const float2 baseCell = floor(grid);
    const float airmass = 1.0f / max(worldDir.y, 0.05f);
    const float twinkleAmount = saturate(0.08f * airmass);
    float3 total = 0.0f.xxx;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const float2 cell = baseCell + float2(dx, dy);
            if (any(cell < 0.0f) || any(cell >= cells))
            {
                continue;
            }
            const float4 random = StarCellRandom(uint3(uint2(cell), face));
            if (random.x > kDensity)
            {
                continue;
            }
            const float2 offset = grid - (cell + 0.15f + random.yz * 0.7f);
            // 一様乱数 u から等級 m = 限界等級 + log10(u) / 0.45（明るい星ほど少ない）。最も明るい星はシリウス程度。
            const float magnitude = max(limitingMagnitude + log10(max(random.w, 1.0e-5f)) / 0.45f, -1.5f);
            // 明るい星ほど写真や目の中で大きく見える（光学系のにじみ）。照度は保ったまま像を広げる。
            // 半径は画素単位。近傍 3x3 のセルに収まるよう上限を付ける。
            const float pixelRadius = min(0.7f * (1.0f + 0.35f * max(4.5f - magnitude, 0.0f)), 0.45f / cellsPerPixel);
            const float2 pixelOffset = float2(gridDy.y * offset.x - gridDy.x * offset.y,
                gridDx.x * offset.y - gridDx.y * offset.x) / (abs(gridDeterminant) > 1.0e-12f ? gridDeterminant : 1.0e-12f);
            const float illuminance = 2.54e-6f * pow(10.0f, -0.4f * magnitude);
            // 大気のゆらぎによる瞬き。平均は 1 のまま、大気の厚い地平線近くほど大きく揺れる。
            const float phase = random.z * 6.2831853f;
            const float rate = 1.5f + 2.5f * random.y;
            const float twinkle = 1.0f + twinkleAmount * sin(gSkyStars.w * rate * 6.2831853f + phase) *
                sin(gSkyStars.w * rate * 4.1f + phase * 1.7f);
            total += StarColor(frac(random.y * 7.31f + random.z * 3.17f)) *
                (illuminance * twinkle * exp(-dot(pixelOffset, pixelOffset) / (pixelRadius * pixelRadius)) /
                    (3.14159265f * pixelRadius * pixelRadius * gridAreaPerPixel));
        }
    }
    // 格子単位の面密度を立体角あたりの輝度へ直し、銀河を足して大気の減光を掛ける。
    return (total / cellSolidAngle + EvaluateMilkyWay(c)) * intensity * HikariNightSkyExtinction(worldDir.y);
}

// 月面の明るさ（満ち欠けと周縁）。円盤の各点の月面法線と太陽の向きから求め、満月の平均が 1 になるようにする。
float MoonSurfaceShading(float3 dir)
{
    const float3 moonDir = HikariAtmosphereSafeNormalize(gSkyDisk.xyz);
    const float radius = clamp(gSkyDisk.w, 1.0e-5f, 0.5f);
    const float3 offset = (dir - moonDir * dot(dir, moonDir)) / sin(radius);
    const float r2 = saturate(dot(offset, offset));
    const float3 normal = normalize(offset - moonDir * sqrt(1.0f - r2));
    const float lit = saturate(dot(normal, HikariAtmosphereSafeNormalize(gSkyMoonPhase.xyz)));
    // 海（暗い領域）を表す穏やかな濃淡。
    const float mare = 0.82f + 0.18f * sin(offset.x * 5.1f + 1.3f) * sin(offset.y * 4.3f + offset.z * 3.7f);
    return lit * mare * 1.5f;
}

// 主光源の円盤（昼は日輪、夜は月）。縁は画素一つ分で切り、日輪には周縁減光を付ける。
// 円盤の平均輝度は gSkyDiskLuminance（大気の透過率込み）に保つ。
float3 AddCelestialDisk(float3 color, float3 dir)
{
    const float3 luminance = max(gSkyDiskLuminance.rgb, 0.0f.xxx);
    if (!all(isfinite(luminance)) || !any(luminance > 0.0f))
    {
        return color;
    }
    const float3 toLight = HikariAtmosphereSafeNormalize(gSkyDisk.xyz);
    const float radius = clamp(gSkyDisk.w, 1.0e-5f, 0.5f);
    dir = normalize(dir);
    // 小さな角度でも桁落ちしないよう、外積の長さと内積から角度を求める。
    const float angle = atan2(length(cross(dir, toLight)), dot(dir, toLight));
    const float edge = saturate((radius - angle) / max(fwidth(angle), 1.0e-7f) + 0.5f);
    if (edge <= 0.0f)
    {
        return color;
    }
    float3 shading;
    if (gSkyMoonPhase.w > 0.5f)
    {
        shading = MoonSurfaceShading(dir).xxx;
    }
    else
    {
        // 周縁減光 1 - u (1 - μ)（μ は円盤上の点での太陽面の法線と視線の余弦）。可視域の係数は青ほど大きい。
        // 円盤平均 1 - u / 3 で割り、平均輝度を変えない。
        static const float3 kLimbDarkening = float3(0.50f, 0.60f, 0.72f);
        const float r = saturate(angle / radius);
        const float mu = sqrt(1.0f - r * r);
        shading = (1.0f - kLimbDarkening * (1.0f - mu)) / (1.0f - kLimbDarkening / 3.0f);
    }
    return color + luminance * shading * edge;
}

float3 SampleAtmosphereSkyView(float3 dir)
{
    return gAtmosphereSkyView.SampleLevel(gLinearClamp, HikariAtmosphereSkyViewUv(dir), 0.0f).rgb;
}

// 天空 LUT の一行を方位について 3 次 B スプラインで読む（線形補間 2 回、GPU Gems 2 の 20 章）。一行の色を下へ
// 延ばすと、線形補間の折れ目が縦の筋に見えるため。
float3 SampleSkyViewRowSmooth(float u, float v, float width)
{
    const float x = u * width - 0.5f;
    const float index = floor(x);
    const float f = x - index;
    const float w0 = (1.0f - f) * (1.0f - f) * (1.0f - f) / 6.0f;
    const float w1 = (4.0f - 6.0f * f * f + 3.0f * f * f * f) / 6.0f;
    const float w3 = f * f * f / 6.0f;
    const float g0 = w0 + w1;
    const float u0 = frac((index - 0.5f + w1 / g0) / width);
    const float u1 = frac((index + 1.5f + w3 / (1.0f - g0)) / width);
    return g0 * gAtmosphereSkyView.SampleLevel(gLinearClamp, float2(u0, v), 0.0f).rgb +
        (1.0f - g0) * gAtmosphereSkyView.SampleLevel(gLinearClamp, float2(u1, v), 0.0f).rgb;
}

// 地平線より下の背景（既定）。地面を描かず、地平線の霞を下へ延ばして真下へ向けて少し暗くする。場面の地面は物体が
// 受け持ち、物体の無い所に遠い地面や雲の影を出さない。地平線の色は天空 LUT の地平線のすぐ下の行（背景の観測者から
// まだ地面に当たらない空気で、雲の下の空気の色も含む）。方位ごとの色は 30 度ほど下までに最も暗い方位の地平線の色へ
// 寄せる。夕日の側の明るい霞が下まで柱のように伸びず、暗い側が下ほど明るくならず、真下で色が一点へ集まる継ぎ目も
// 作らない。寄せ方は対数で補間し（明るさの比を一様に縮める）、明るい側が最後に急に暗くなって影の縁に見えないように
// する。
float3 EvaluateBelowHorizonHaze(float3 dir)
{
    static const float kNadirBrightness = 0.6f;
    const float depression = saturate(-dir.y);
    float3 horizon;
    float3 darkest;
    if (gAtmosphereSunDirection.w > 1.5f)
    {
        uint width = 0u;
        uint height = 0u;
        gAtmosphereSkyView.GetDimensions(width, height);
        const float v = 0.5f - 0.5f / float(max(height, 1u));
        horizon = SampleSkyViewRowSmooth(HikariAtmosphereSkyViewUv(dir).x, v, float(max(width, 1u)));
        darkest = horizon;
        float darkestLuminance = 3.4e38f;
        [unroll] for (uint index = 0u; index < 8u; ++index)
        {
            const float3 color =
                gAtmosphereSkyView.SampleLevel(gLinearClamp, float2((float(index) + 0.5f) / 8.0f, v), 0.0f).rgb;
            const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
            if (luminance < darkestLuminance)
            {
                darkest = color;
                darkestLuminance = luminance;
            }
        }
    }
    else
    {
        // LUT の無い間の代替は方位ごとの色だけを使う。
        const float horizontalSquared = dot(dir.xz, dir.xz);
        horizon = EvaluateAtmosphereSky(horizontalSquared > 1.0e-8f
            ? float3(dir.x, 0.0f, dir.z) * rsqrt(horizontalSquared)
            : float3(1.0f, 0.0f, 0.0f));
        darkest = horizon;
    }
    const float toward = smoothstep(0.0f, 0.5f, depression);
    const float3 haze = exp(lerp(log(max(horizon, 1.0e-6f.xxx)), log(max(darkest, 1.0e-6f.xxx)), toward));
    return haze * lerp(1.0f, kNadirBrightness, smoothstep(0.0f, 1.0f, depression));
}

float4 main(PSInput input) : SV_TARGET
{
    float3 worldRayDir = ReconstructWorldRay(input.ndc);
    float3 skySampleDir = normalize(HikariRotateSkyYaw(worldRayDir, gSkyScaleYaw.y));
    uint mode = (uint)(gSkyTintMode.a + 0.5f);

    float3 sky = 0.0f.xxx;
    if (mode == 4u && gAtmosphereSunDirection.w > 0.5f)
    {
        if (worldRayDir.y < 0.0f && gSkyScaleYaw.w < 0.5f)
        {
            sky = EvaluateBelowHorizonHaze(worldRayDir);
        }
        else
        {
            sky = gAtmosphereSunDirection.w > 1.5f
                ? SampleAtmosphereSkyView(worldRayDir)
                : EvaluateAtmosphereSky(worldRayDir);
        }
        // 星と夜空の背景は地平線より下で 0。
        sky += EvaluateStars(worldRayDir) + HikariNightSkyBackground(worldRayDir, gSkyStars.z);
    }
    else if (mode == 2u)
    {
        sky = gSkyCube.Sample(gLinearClamp, skySampleDir).rgb;
    }
    else if (mode == 3u)
    {
        const float2 panoramaUv =
            HikariDirectionToEquirectangularUv(skySampleDir);
        float2 panoramaDerivativeX;
        float2 panoramaDerivativeY;
        HikariResolveEquirectangularGradients(
            skySampleDir,
            panoramaUv,
            panoramaDerivativeX,
            panoramaDerivativeY);
        sky = gSkyTex2D.SampleGrad(
            gLinearClamp,
            panoramaUv,
            panoramaDerivativeX,
            panoramaDerivativeY).rgb;
    }

    sky = HikariApplySkyRadianceScale(sky, gSkyTintMode.rgb, gSkyScaleYaw.x);
    // 円盤は絶対量の輝度なので、空の画素値の倍率と色味の後に足す。
    sky = AddCelestialDisk(sky, worldRayDir);
    if (!all(isfinite(sky)))
    {
        sky = 0.0f.xxx;
    }
    sky = max(sky, 0.0f.xxx);
    // 太陽の円盤は Pre-Exposure 後も FP16 の上限を超えうる。白より十分上で止める。
    return float4(min(sky * gSkyScaleYaw.z, 60000.0f.xxx), 1.0f);
}
