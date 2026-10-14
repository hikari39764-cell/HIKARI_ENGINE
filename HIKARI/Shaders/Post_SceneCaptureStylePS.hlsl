#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"

Texture2D<float4> gCapture : register(t0);
SamplerState gSampler : register(s0);

// 画面（電視など）に貼るキャプチャだけの描き方。主画面の後処理には登録しない。
// 入力は前露出の HDR、出力はスクリーンの Unlit 材質が読む表示参照の線形色。
// 物体の色は作者が陰影を焼いた少数の色である前提で、線形輝度が backgroundLuminance 未満を背景（壁）、以上を物体とする。
// sRGB の彩度差（最大−最小）が 0.3 以上の物体は「鮮やかな物体」（主角・残影・危険など）として目立たせる。
// gUser[0]: 種類（1 Clean / 2 Pixel / 3 Sketch）、pixelSize（texel / ゲーム画素）、lineWidth（ゲーム画素）、
//           shadowStrength（Pixel は落ち影の濃さ、Sketch は纵深の強さ）
// gUser[1]: lineColor（sRGB）、brightness
// gUser[2]: paperColor（sRGB）、1 / 露出
// gUser[3]: accentColor（sRGB。この色相の鮮やかな物体を残影として横縞で描く）、backgroundLuminance
// gUser[4]: 落ち影の長さ（ゲーム画素、右と下が正）。物体からこの向きへ伸びる帯が影になる
static const uint kStylePixel = 2u;
static const uint kStyleSketch = 3u;

float3 StyleLinear(float3 srgb)
{
    return float3(srgb.r <= 0.04045 ? srgb.r / 12.92 : pow((srgb.r + 0.055) / 1.055, 2.4),
                  srgb.g <= 0.04045 ? srgb.g / 12.92 : pow((srgb.g + 0.055) / 1.055, 2.4),
                  srgb.b <= 0.04045 ? srgb.b / 12.92 : pow((srgb.b + 0.055) / 1.055, 2.4));
}

// 草図は紙に色を重ねる描き方なので、混ぜ合わせを表示の sRGB で行う（線形で混ぜると明るい象牙が強く出すぎる）。
float3 StyleSrgb(float3 linearColor)
{
    const float3 c = saturate(linearColor);
    return float3(c.r <= 0.0031308 ? c.r * 12.92 : 1.055 * pow(c.r, 1.0 / 2.4) - 0.055,
                  c.g <= 0.0031308 ? c.g * 12.92 : 1.055 * pow(c.g, 1.0 / 2.4) - 0.055,
                  c.b <= 0.0031308 ? c.b * 12.92 : 1.055 * pow(c.b, 1.0 / 2.4) - 0.055);
}

float2 StyleResolution()
{
    return max(float2(gResolutionX, gResolutionY), 1.0);
}

// texel 座標（左上原点、texel 中心は +0.5）の線形色。露出を戻し、範囲外は端の texel で止める。
float3 ReadTexel(float2 texel)
{
    const float2 resolution = StyleResolution();
    const float2 uv = clamp(texel, 0.5, resolution - 0.5) / resolution;
    const float3 color = gCapture.SampleLevel(gSampler, uv, 0).rgb;
    return max(any(isnan(color)) ? 0.0 : color, 0.0) * gUser[2].w;
}

float StyleLuminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

bool IsObject(float3 color)
{
    return StyleLuminance(color) >= gUser[3].w;
}

bool IsDanger(float3 color)
{
    return color.r > 1e-5 && color.r > 1.6 * max(color.g, color.b);
}

float StyleChroma(float3 srgb)
{
    return max(srgb.r, max(srgb.g, srgb.b)) - min(srgb.r, min(srgb.g, srgb.b));
}

// 色相（0..1）。彩度が無ければ 0。
float StyleHue(float3 srgb)
{
    const float high = max(srgb.r, max(srgb.g, srgb.b));
    const float chroma = StyleChroma(srgb);
    if (chroma <= 1e-5)
        return 0.0;
    const float hue = high == srgb.r ? (srgb.g - srgb.b) / chroma
                    : high == srgb.g ? 2.0 + (srgb.b - srgb.r) / chroma
                                     : 4.0 + (srgb.r - srgb.g) / chroma;
    return frac(hue / 6.0 + 1.0);
}

// 鮮やかな物体：背景・墨・青緑の躯体・象牙より彩度がはっきり高い色（主角・残影・危険）。
bool IsVivid(float3 color)
{
    return IsObject(color) && StyleChroma(StyleSrgb(color)) >= 0.3;
}

// 残影：accentColor と同じ色相の鮮やかな物体。
bool IsGhost(float3 color)
{
    if (!IsVivid(color))
        return false;
    const float difference = abs(StyleHue(StyleSrgb(color)) - StyleHue(gUser[3].rgb));
    return min(difference, 1.0 - difference) < 0.06;
}

// 焼いた三段のうち一番暗い段か。象牙（明るく彩度が低い）、赤、それ以外（墨・青緑）で境の明るさを変える。
bool IsShadowBand(float3 color)
{
    const float luminance = StyleLuminance(color);
    if (IsDanger(color))
        return luminance < 0.08;
    const float saturation = max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
    if (luminance > 0.2 && saturation < 0.25 * luminance + 0.05)
        return luminance < 0.45;
    return luminance < 0.06;
}

// 落ち影：ゲーム画素 cell から光の側（offset の逆向き）へ 1 ゲーム画素ずつ辿り、途中に物体があれば影。
// 箱を奥の壁へ斜めに投げた影と同じく、物体に接したまま offset の長さまで伸びる帯になる。
bool InCastShadow(float2 cell, float2 offset, float block)
{
    const int steps = (int)max(abs(offset.x), abs(offset.y));
    [loop] for (int step = 1; step <= steps; ++step)
    {
        const float2 source = cell - round(offset * ((float)step / (float)steps));
        if (IsObject(ReadTexel((source + 0.5) * block)))
            return true;
    }
    return false;
}

// 影の濃さ：0 なし、1 本影、0.5 影の外周 1 ゲーム画素（ドット絵の市松で混ぜる所）。
float CastShadow(float2 cell, float block)
{
    const float2 offset = round(gUser[4].xy);
    if (gUser[0].w <= 0.0 || all(offset == 0.0))
        return 0.0;
    if (InCastShadow(cell, offset, block))
        return 1.0;
    return InCastShadow(cell, offset + sign(offset), block) ? 0.5 : 0.0;
}

// 一番近い鮮やかな物体（step 単位、上下左右の距離 reach 以内）。rgb はその線形色、a は距離（無ければ 0）。
float4 NearestVivid(float2 center, float step, int reach)
{
    float4 nearest = 0.0;
    [loop] for (int dy = -reach; dy <= reach; ++dy)
    {
        [loop] for (int dx = -reach; dx <= reach; ++dx)
        {
            const int distance = abs(dx) + abs(dy);
            if (distance == 0 || distance > reach || (nearest.a > 0.0 && distance >= nearest.a))
                continue;
            const float3 other = ReadTexel(center + float2(dx, dy) * step);
            if (IsVivid(other))
                nearest = float4(other, distance);
        }
    }
    return nearest;
}

// ---------------------------------------------------------------- Pixel
// ゲーム画素（pixelSize texel 四方）の中心の色を一面に塗る。背景の升目は、上下左右 lineWidth 以内に物体があれば
// 輪郭の色（外側だけに線を引き、物体の縁の画素は削らない）。落ち影の中は輪郭の色へ shadowStrength だけ寄せ、
// 壁の目盛りや光の輪を沈める。影の外周 1 ゲーム画素は市松にして段差をドット絵の階調で繋ぐ。
// 鮮やかな物体の輪郭の外 2 ゲーム画素にはその色の淡い光（外側は市松）を置き、残影は 1 ゲーム画素おきの横縞にする。
float3 StylePixel(float2 position)
{
    const float block = max(round(gUser[0].y), 1.0);
    const float2 cell = floor(position / block);
    const float3 color = ReadTexel((cell + 0.5) * block);
    const bool checker = fmod(cell.x + cell.y, 2.0) < 0.5;
    if (IsObject(color))
        return IsGhost(color) && fmod(cell.y, 2.0) >= 0.5 ? color * 0.55 : color;
    const float3 outline = StyleLinear(gUser[1].rgb);
    const int reach = (int)round(gUser[0].z);
    [loop] for (int distance = 1; distance <= reach; ++distance)
    {
        const float2 offsets[4] = {float2(distance, 0), float2(-distance, 0), float2(0, distance),
                                   float2(0, -distance)};
        [unroll] for (int i = 0; i < 4; ++i)
        {
            if (IsObject(ReadTexel((cell + offsets[i] + 0.5) * block)))
                return outline;
        }
    }
    const float shadow = CastShadow(cell, block);
    float3 result = shadow >= 1.0 || (shadow > 0.0 && checker) ? lerp(color, outline, gUser[0].w) : color;
    const float4 glow = NearestVivid((cell + 0.5) * block, block, reach + 2);
    if (glow.a > reach + 1.5)
        return checker ? lerp(result, glow.rgb * 0.75, 0.22) : result;
    if (glow.a > 0.0)
        return lerp(result, glow.rgb * 0.75, 0.35);
    return result;
}

// ---------------------------------------------------------------- Sketch
// 鉛筆線の種類：0 無し、1 物体の中で焼いた色の段が変わる所（右と下の隣だけと比べて片側に 1 本）、
// 2 物体の外形（背景の側に 1 本）。両側に引くと線が倍の太さになり、細い足場の上面が塗り潰されて見える。
// neighbor は外形の時に接している物体の色（鮮やかな物体は線をその色の色鉛筆で引くため）。
int SketchEdge(float2 position, float3 color, float distance, out float3 neighbor)
{
    neighbor = 0.0;
    const bool object = IsObject(color);
    const float2 offsets[4] = {float2(distance, 0), float2(0, distance), float2(-distance, 0), float2(0, -distance)};
    int edge = 0;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        const float3 other = ReadTexel(position + offsets[i]);
        const bool otherObject = IsObject(other);
        if (!object && otherObject)
        {
            neighbor = other;
            return 2;
        }
        if (object && otherObject && i < 2 && any(abs(sqrt(color) - sqrt(other)) > 0.06))
            edge = 1;
    }
    return edge;
}

// 接地：背景の texel のすぐ周り（2 texel、5 texel）に物体があれば、その分だけ壁を暗くする割合。
float SketchContact(float2 position)
{
    const float2 directions[8] = {float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
                                  float2(1, 1), float2(1, -1), float2(-1, 1), float2(-1, -1)};
    [unroll] for (int i = 0; i < 8; ++i)
    {
        if (IsObject(ReadTexel(position + directions[i] * 2.0)))
            return 0.16;
    }
    [unroll] for (int j = 0; j < 8; ++j)
    {
        if (IsObject(ReadTexel(position + directions[j] * 5.0)))
            return 0.07;
    }
    return 0.0;
}

// 紙（paperColor）に鉛筆（lineColor）。混ぜ合わせは sRGB。物体は焼いた色を 32%（明るい天板は 55%）乗せ、暗い段に細い斜線。
// 鮮やかな物体は色鉛筆で自分の色をしっかり塗り、周りの紙にその色を淡く滲ませる。残影は 2 texel おきの横縞で抜く。
// 纵深（shadowStrength）：暗い段を紙より沈め、壁の落ち影・物体の周りの接地の暗さ・光の輪の淡い白墨を描き、
// 外形の線を太く、物体の中の線を細くする。0 なら平らな草図。
// 線は texel ごとに 7% かすれ、右下へ 1 texel ずらした薄い二度描きを重ねる。鮮やかな物体の外形はその色の色鉛筆。
float3 SketchInk(float3 pencil, float3 neighbor)
{
    return IsVivid(neighbor) ? lerp(StyleSrgb(neighbor), pencil, 0.2) : pencil;
}

float3 StyleSketch(float2 position)
{
    const float block = max(gUser[0].y, 1.0);
    const float depth = saturate(gUser[0].w);
    const float3 paper = gUser[2].rgb;
    const float3 pencil = gUser[1].rgb;
    const float3 color = ReadTexel(position);
    const float2 texel = floor(position);
    const bool object = IsObject(color);
    const bool vivid = IsVivid(color);
    // 紙のむら：2 texel ごとに固定の揺らぎ（時間では動かさない）。
    const float grain = HikariHash12(floor(position * 0.5)) * 2.0 - 1.0;
    float3 result = paper + grain * 0.012;
    if (vivid)
    {
        result = lerp(result, StyleSrgb(color), 0.95);
        if (IsGhost(color) && fmod(floor(texel.y * 0.5), 2.0) >= 0.5)
            result = lerp(result, paper, 0.5);
    }
    else if (object)
    {
        const float3 tone = StyleSrgb(color);
        // 読みやすさ：明るく彩度の低い面（象牙の天板＝立てる所）は白墨で厚く塗り、躯体も紙より一段はっきり乗せる。
        const bool lightSurface = StyleLuminance(color) > 0.35 && StyleChroma(tone) < 0.2;
        result = lerp(result, tone, lightSurface ? 0.55 : 0.32);
        if (IsShadowBand(color))
        {
            result = lerp(result, paper * 0.82 + tone * 0.10, depth);
            if (fmod(texel.x + texel.y, 4.0) < 0.5)
                result = lerp(result, pencil, 0.45 - 0.13 * depth);
        }
    }
    else
    {
        // 背景：光の輪が明るい所ほど白墨を淡く掃く（空気の層）。落ち影は一段暗く疎らな斜線、物体の周りは接地の暗さ。
        const float lift = saturate(StyleLuminance(color) / max(gUser[3].w, 1e-4) * 1.2 - 0.35);
        result = lerp(result, pencil, 0.12 * lift * depth);
        if (depth > 0.0 && CastShadow(floor(position / block), block) >= 1.0)
        {
            result *= 1.0 - 0.38 * depth;
            if (fmod(texel.x - texel.y + 4096.0, 6.0) < 0.5)
                result = lerp(result, pencil, 0.08 * depth);
        }
        result *= 1.0 - depth * SketchContact(position);
        const float4 glow = NearestVivid(position, 1.0, 4);
        if (glow.a > 0.0)
            result = lerp(result, StyleSrgb(glow.rgb), 0.28 * (1.0 - (glow.a - 1.0) / 4.0));
    }
    if (gUser[0].z > 0.0)
    {
        const float distance = max(round(gUser[0].z * block * 0.5), 1.0);
        float3 neighbor;
        const int edge = SketchEdge(position, color, distance, neighbor);
        if (edge == 2 || (edge == 1 && !vivid))
        {
            if (HikariHash12(texel + 17.0) < 0.93)
                result = lerp(result, SketchInk(pencil, neighbor), edge == 2 ? 0.9 : lerp(0.85, 0.72, depth));
        }
        else if (!object && depth > 0.0 && SketchEdge(position, color, distance + 1.0, neighbor) == 2)
        {
            result = lerp(result, SketchInk(pencil, neighbor), 0.42 * depth);   // 外形をもう 1 texel 太く
        }
        else
        {
            const float2 echo = position - 1.0;
            if (HikariHash12(texel + 3.0) < 0.5 && SketchEdge(echo, ReadTexel(echo), distance, neighbor) == 2)
                result = lerp(result, SketchInk(pencil, neighbor), 0.4);
        }
    }
    return StyleLinear(saturate(result));
}

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    const uint style = (uint)round(gUser[0].x);
    const float2 position = floor(input.position.xy) + 0.5;
    float3 result;
    if (style == kStylePixel)
        result = StylePixel(position);
    else if (style == kStyleSketch)
        result = StyleSketch(position);
    else
        result = ReadTexel(position);   // Clean：3D 描画をそのまま
    return float4(result * gUser[1].w, 1.0);
}
