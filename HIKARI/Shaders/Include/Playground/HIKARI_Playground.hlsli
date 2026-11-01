#ifndef HIKARI_PLAYGROUND_HLSLI
#define HIKARI_PLAYGROUND_HLSLI

// ShaderPlayground の試し書き。.hlsl の先頭でこれを include し、次の関数を一つ書く：
//
//     float4 Play(PlaygroundSample s) { ... }
//
// rgb は線形の色、a は場面の背景への重なり（1 で不透明）。共通関数（Include/Lib/HIKARI_ShaderLib.hlsli）と
// 下の補助関数が使える。試験の設定（.json）の parameters は PLAY_PARAM(s, 名前) で float4、PLAY_FLOAT(s, 名前) で
// その x を読む。各タイルの値と場面は C++ の Editor/ShaderPlayground が詰める（並びは下の kPlayground* と対）。
#include "Include/Lib/HIKARI_ShaderLib.hlsli"

static const uint kPlaygroundSceneSphere = 0u;  // 正射影の単位球。光は s.light から
static const uint kPlaygroundSceneFurnace = 1u; // 白い環境だけで照らした球（エネルギーが 1 を超えないかを見る）
static const uint kPlaygroundSceneUv = 2u;      // タイル全面に uv
static const uint kPlaygroundSceneImage = 3u;   // s.image に入力画像（無ければ試験図）
static const uint kPlaygroundSceneCurve = 4u;   // Play の rgb を s.x の関数として描く
static const uint kPlaygroundSceneRamp = 5u;    // 平らな面で、光の当たり（ndotl）が左 0 から右 1 へ変わる帯

static const uint kPlaygroundMaxTiles = 64u;
static const uint kPlaygroundTileSlots = 16u;
static const uint kPlaygroundParamSlots = 12u;

struct PlaygroundSample
{
    float2 uv;          // タイル内 0..1（左上が原点、y は下向き）
    float2 pixel;       // タイル内の画素位置
    float2 tileSize;    // タイルの画素数
    float2 screenUv;    // シート全体の 0..1
    uint tile;          // タイル番号（左上から右へ、行ごとに下へ）
    uint tileCount;
    float x;            // 横の位置 0..1（曲線の横軸）
    float sweep;        // 掃引の値（設定の from..to）
    float sweepT;       // 掃引の中の位置 0..1
    float time;         // 秒
    bool hit;           // 球に当たったか（球の場面以外は常に true）
    float3 position;    // 球面上の位置（中心が原点、半径 1、視点空間：x 右、y 上、z 手前）
    float3 normal;
    float3 view;        // 面から視点へ
    float3 light;       // 面から光へ
    float3 lightColor;  // 光の放射輝度（色 × 強さ）
    float3 ambient;     // 環境光（一様）
    float3 baseColor;
    float roughness;
    float metallic;
    float ndotl;
    float3 image;       // 画像の場面の入力色（線形）
    float4 params[kPlaygroundParamSlots];
};

#define PLAY_PARAM(s, name) ((s).params[HIKARI_PLAY_PARAM_##name])
#define PLAY_FLOAT(s, name) (PLAY_PARAM(s, name).x)

float4 Play(PlaygroundSample s);

// ---- 補助関数 ----

// サンプルの材質と向きの PBR 文脈（誘電体の F0 は 0.04）。
HikariPbrShadingContext PlaygroundPbrContext(PlaygroundSample s)
{
    return HikariBuildPbrShadingContext(s.baseColor, s.metallic, s.roughness, 1.0f.xxx, 1.0f, s.normal, s.view);
}

// 標準の PBR（GGX と Lambert、主光と一様な環境光）。エンジンの前向きの着色と同じ BRDF。
float3 PlaygroundShadePbr(PlaygroundSample s)
{
    const HikariPbrShadingContext context = PlaygroundPbrContext(s);
    const HikariDirectPbrLobes direct = HikariEvaluateDirectPbrLobes(context, s.light, s.lightColor);
    const float3 ambient = s.ambient * (context.diffuseEnergyWeight * context.baseColor +
                                        context.environmentSpecularResponse);
    return direct.diffuse + direct.specular + ambient;
}

// 0..1 の値を青→緑→黄→赤の偽色で見せる（範囲の外は端の色）。
float3 PlaygroundHeat(float v)
{
    const float t = saturate(v);
    return saturate(float3(1.5f - abs(4.0f * t - 3.0f), 1.5f - abs(4.0f * t - 2.0f), 1.5f - abs(4.0f * t - 1.0f)));
}

// 市松模様（cells は一辺の数）。
float PlaygroundChecker(float2 uv, float cells)
{
    const float2 c = floor(uv * cells);
    return fmod(c.x + c.y, 2.0f);
}

// 入力画像が無いときの試験図：上から色相、灰の段階（線形 0..1）、原色の帯、明るい点（HDR 4.0）。
float3 PlaygroundTestImage(float2 uv)
{
    if (uv.y < 0.25f)
        return HikariHsvToRgb(float3(uv.x, 1.0f, 1.0f));
    if (uv.y < 0.5f)
        return uv.x.xxx;
    if (uv.y < 0.75f)
    {
        static const float3 kBars[8] = {float3(1, 1, 1), float3(1, 1, 0), float3(0, 1, 1), float3(0, 1, 0),
                                        float3(1, 0, 1), float3(1, 0, 0), float3(0, 0, 1), float3(0, 0, 0)};
        return kBars[min(uint(uv.x * 8.0f), 7u)] * 0.8f;
    }
    const float2 spot = (uv - float2(0.5f, 0.875f)) * 8.0f;
    return lerp(0.18f.xxx, 0.22f.xxx, PlaygroundChecker(uv, 16.0f)) + 4.0f * saturate(1.0f - length(spot) * 2.0f);
}

// ---- 入口（C++ の ShaderPlaygroundRunner と対） ----

cbuffer PlaygroundGlobals : register(b0)
{
    float2 gPlaySheetSize;
    float2 gPlayTileSize;
    uint gPlayColumns;
    uint gPlayTileCount;
    uint gPlayScene;
    uint gPlayHasImage;
    float3 gPlayAmbientColor;
    float gPlayGap;
    float2 gPlayCurveRange;
    float gPlayExposure;
    float gPlayPadding;
    // タイルごと：0..11 parameters、12 = baseColor, roughness、13 = metallic, sweep, sweepT, time、
    // 14 = 光へ向かう方向, 環境光の強さ、15 = 光の放射輝度, 未使用。
    float4 gPlayTiles[kPlaygroundMaxTiles * kPlaygroundTileSlots];
};

Texture2D<float4> gPlayImage : register(t0);
SamplerState gPlaySampler : register(s0);

struct PlaygroundOutput
{
    float4 display : SV_Target0; // 表示用：背景に重ねた色（露出込み）、a は被写体の画素なら 1
    float4 raw : SV_Target1;     // Play の戻り値そのまま（統計と比較に使う）
};

float4 PlaygroundVS(uint vertexId : SV_VertexID) : SV_Position
{
    const float2 uv = float2((vertexId << 1u) & 2u, vertexId & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float3 PlaygroundPlotCurve(float2 uv, float3 value)
{
    const float y = lerp(gPlayCurveRange.y, gPlayCurveRange.x, uv.y);
    const float yPixel = max(fwidth(y), 1.0e-6f);
    float3 color = 0.03f.xxx;
    color += 0.12f * (1.0f - saturate(abs(y) / yPixel - 0.5f));
    color += 0.12f * (1.0f - saturate(abs(y - 1.0f) / yPixel - 0.5f));
    const float xGrid = abs(frac(uv.x * 4.0f + 0.5f) - 0.5f) / max(fwidth(uv.x * 4.0f), 1.0e-6f);
    color += 0.05f * (1.0f - saturate(xGrid - 0.5f));
    static const float3 kChannel[3] = {float3(1.0f, 0.25f, 0.2f), float3(0.3f, 1.0f, 0.3f), float3(0.3f, 0.5f, 1.0f)};
    [unroll]
    for (uint channel = 0u; channel < 3u; ++channel)
    {
        const float d = value[channel] - y;
        const float stroke = 1.0f - saturate(abs(d) / max(fwidth(d), 1.0e-6f) - 0.75f);
        color = max(color, kChannel[channel] * stroke);
    }
    return color;
}

PlaygroundOutput PlaygroundPS(float4 position : SV_Position)
{
    const float2 cell = gPlayTileSize + gPlayGap;
    const uint2 tileCoord = uint2(position.xy / cell);
    const float2 local = position.xy - float2(tileCoord) * cell;
    const uint tileIndex = tileCoord.y * gPlayColumns + tileCoord.x;
    const bool inTile = tileCoord.x < gPlayColumns && tileIndex < gPlayTileCount && all(local < gPlayTileSize);
    const uint tile = min(tileIndex, gPlayTileCount - 1u);
    const uint base = tile * kPlaygroundTileSlots;

    PlaygroundSample s;
    s.uv = local / gPlayTileSize;
    s.pixel = local;
    s.tileSize = gPlayTileSize;
    s.screenUv = position.xy / gPlaySheetSize;
    s.tile = tile;
    s.tileCount = gPlayTileCount;
    s.x = s.uv.x;
    [unroll]
    for (uint slot = 0u; slot < kPlaygroundParamSlots; ++slot)
        s.params[slot] = gPlayTiles[base + slot];
    const float4 material = gPlayTiles[base + 12u];
    const float4 sweep = gPlayTiles[base + 13u];
    const float4 light = gPlayTiles[base + 14u];
    s.baseColor = material.rgb;
    s.roughness = material.w;
    s.metallic = sweep.x;
    s.sweep = sweep.y;
    s.sweepT = sweep.z;
    s.time = sweep.w;
    s.light = normalize(light.xyz);
    s.lightColor = gPlayTiles[base + 15u].rgb;
    s.ambient = gPlayAmbientColor * light.w;
    s.image = gPlayHasImage != 0u ? gPlayImage.SampleLevel(gPlaySampler, s.uv, 0.0f).rgb : PlaygroundTestImage(s.uv);

    const bool sphere = gPlayScene == kPlaygroundSceneSphere || gPlayScene == kPlaygroundSceneFurnace;
    const float2 p = (s.uv * 2.0f - 1.0f) * float2(1.08f, -1.08f);
    const float r2 = dot(p, p);
    s.hit = !sphere || r2 <= 1.0f;
    s.normal = sphere ? normalize(float3(p, sqrt(max(1.0f - r2, 0.0f)) + 1.0e-4f)) : float3(0.0f, 0.0f, 1.0f);
    s.position = s.normal;
    s.view = float3(0.0f, 0.0f, 1.0f);
    if (gPlayScene == kPlaygroundSceneFurnace)
    {
        s.lightColor = 0.0f.xxx;
        s.ambient = 1.0f.xxx;
    }
    if (gPlayScene == kPlaygroundSceneRamp)
        s.light = float3(sqrt(max(1.0f - s.x * s.x, 0.0f)), 0.0f, s.x);
    s.ndotl = saturate(dot(s.normal, s.light));

    // Play は全画素で呼ぶ（球の外も s.hit = false で呼ぶので、微分は画素の 2x2 で揃う）。
    const float4 value = Play(s);
    PlaygroundOutput output;
    output.raw = value;
    const float3 background = lerp(0.16f.xxx, 0.2f.xxx, PlaygroundChecker(s.uv, 8.0f));
    float3 shown;
    if (gPlayScene == kPlaygroundSceneCurve)
        shown = PlaygroundPlotCurve(s.uv, value.rgb);
    else
        shown = lerp(background, value.rgb * gPlayExposure, s.hit ? saturate(value.a) : 0.0f);
    output.display = float4(shown, s.hit ? 1.0f : 0.0f);
    if (!inTile)
    {
        output.display = float4(0.06f.xxx, 0.0f);
        output.raw = 0.0f.xxxx;
    }
    return output;
}

#endif
