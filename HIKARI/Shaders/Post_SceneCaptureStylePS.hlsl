#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"
#include "Include/Color/HIKARI_Luminance.hlsli"

Texture2D<float4> gCapture : register(t0);
Texture2D<float> gDepth : register(t1);   // キャプチャの深度（0 近い〜1 遠い）。gUser[9].x が 0 なら無い
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
// gUser[4]: 落ち影の長さ（ゲーム画素、右と下が正）。物体からこの向きへ伸びる帯が影になる。z は Sketch の塗りの濃さ
// 深度（版 4）。距離は world 単位、奥行きは前景の面（depthFocus）から奥を正とする。
// gUser[5]: depthFocus、backgroundDepth、fogStrength、fogDepth
// gUser[6]: fogColor（sRGB）、occlusion
// gUser[7]: occlusionRadius（ゲーム画素）、occlusionDepth、depthLine、lightShadow
// gUser[8]: lightSlope、lightReach（ゲーム画素）、カメラの nearZ、farZ
// gUser[9]: 深度がある（1）、正投影（1）、depthLineGap、背景を透かす（版 5）
// 色調（版 6）。鮮やかな物体とその縁の外の画素だけに掛ける。
// gUser[10]: toneTint（sRGB）、toneTintStrength
// gUser[11]: toneHue（一周 = 1）、toneSaturation、toneBrightness、toneContrast
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

bool IsObject(float3 color)
{
    return HikariLuminance(color) >= gUser[3].w;
}

// ---------------------------------------------------------------- 深度
bool DepthOn()
{
    return gUser[9].x > 0.5 && gUser[5].x > 0.0;
}

// texel の前景の面からの奥行き（world 単位、手前が負）。何も描かれていない所は far の奥行き。
float DepthBehind(float2 texel)
{
    const float2 resolution = StyleResolution();
    const int2 p = int2(clamp(floor(texel), 0.0, resolution - 1.0));
    const float d = gDepth.Load(int3(p, 0));
    const float nearZ = gUser[8].z;
    const float farZ = gUser[8].w;
    const float distance = gUser[9].y > 0.5 ? nearZ + d * (farZ - nearZ)
                                             : nearZ * farZ / max(farZ - d * (farZ - nearZ), 1e-6);
    return distance - gUser[5].x;
}

// 深度で分ける時も、輝度がこれ以上の画素（光る演出など、深度を書かない物）は物体として扱う。
static const float kAlwaysObjectLuminance = 0.75;

// 物体か（深度で分けられる時は前景からの奥行き、無ければ輝度）。
bool ObjectAt(float2 texel, float3 color)
{
    if (DepthOn() && gUser[5].y > 0.0)
        return DepthBehind(texel) < gUser[5].y || HikariLuminance(color) >= kAlwaysObjectLuminance;
    return IsObject(color);
}

// 霧：背景の奥行きが backgroundDepth から fogDepth へ進むほど濃くなる。0..fogStrength
float FogAmount(float2 texel)
{
    if (!DepthOn() || gUser[5].z <= 0.0)
        return 0.0;
    const float start = gUser[5].y;
    return gUser[5].z * smoothstep(start, max(gUser[5].w, start + 1e-3), DepthBehind(texel));
}

// 遮蔽：周り二重の輪の標本のうち、この面より手前にある分だけ暗くする（物の周りの奥の面、入隅）。0..occlusion
float Occlusion(float2 texel, float block)
{
    if (!DepthOn() || gUser[6].w <= 0.0)
        return 0.0;
    const float center = DepthBehind(texel);
    const float radius = max(gUser[7].x * block, 1.0);
    const float range = max(gUser[7].y, 1e-3);
    const float2 directions[8] = {float2(1, 0), float2(0.7071, 0.7071), float2(0, 1), float2(-0.7071, 0.7071),
                                  float2(-1, 0), float2(-0.7071, -0.7071), float2(0, -1), float2(0.7071, -0.7071)};
    float sum = 0.0;
    [unroll] for (int i = 0; i < 8; ++i)
    {
        [unroll] for (int ring = 1; ring <= 2; ++ring)
        {
            const float difference = center - DepthBehind(texel + directions[i] * radius * (ring * 0.5));
            // 段差が大きすぎる（はるか奥の面から前景を見る）所は弱める：窓の向こうが枠の周りで黒く縁取られない
            sum += saturate(difference / range) * (difference < range * 3.0 ? 1.0 : 0.25);
        }
    }
    return saturate(sum / 16.0 * 1.6) * gUser[6].w;
}

// 光の影：光へ向かって（shadowOffset の逆向き）step texel ずつ進み、光線（1 ゲーム画素ごとに lightSlope だけ手前へ上がる）
// より手前に面があれば影。物の高さ（奥の面との奥行きの差）が大きいほど影が長くなる。0..lightShadow
// step を 1 texel にすると影の縁がゲーム画素の段にならない（素描）。ピクセルはゲーム画素ずつ（step = block）。
float LightShadowStep(float2 texel, float block, float step)
{
    if (!DepthOn() || gUser[7].w <= 0.0 || all(gUser[4].xy == 0.0))
        return 0.0;
    const float2 toward = -normalize(gUser[4].xy) * step;
    const float slope = gUser[8].x * step / block;
    const int reach = (int)min(round(gUser[8].y * block / step), 64.0);
    const float start = DepthBehind(texel);
    // 前景の正面（奥行きがほぼ 0 の面）は影を受けない：光は手前上から当たり、正面は明るい。人物などは前景より手前に
    // 置いてあるので、受けさせると床の天板から正面へ影がこぼれる。
    if (gUser[5].y > 0.0 && abs(start) < gUser[5].y * 0.12)
        return 0.0;
    // 物の厚み：深度は手前の面しか持たないので、光線より手前にあってもこれより離れた物（はるか手前に浮く物）は
    // 光線の上を通り過ぎたものとして影にしない（奥の大広間に前景の大きな影が落ちない）。前景の奥行きを厚みとする。
    const float thickness = gUser[5].y > 0.0 ? gUser[5].y : 1e9;
    // 光線の横に 2・4.5 ゲーム画素離した標本も見る（物の継ぎ目や丸めた角から光が漏れて影の縁が波打たない）。
    const float2 side = float2(-toward.y, toward.x) / max(step, 1e-3) * (block * 2.0);
    [loop] for (int k = 1; k <= reach; ++k)
    {
        const float ray = start - (k + 0.5) * slope;
        // 光線が一番手前の物（前景の奥行きの分だけ手前）より手前へ出たら、もう遮る物は無い。
        if (ray < -thickness)
            break;
        const float2 p = texel + toward * k;
        const float d = min(min(DepthBehind(p), min(DepthBehind(p + side), DepthBehind(p - side))),
                            min(DepthBehind(p + side * 2.25), DepthBehind(p - side * 2.25)));
        if (d < ray && ray - d < thickness)
            return gUser[7].w;
    }
    return 0.0;
}

float LightShadow(float2 texel, float block)
{
    return LightShadowStep(texel, block, block);
}

// 奥の層（前景の奥行きの 1.8 倍より奥。窓の向こうの大広間など）か。素描はここに影の斜線も段差の線も引かず、淡い塗りだけにする。
bool FarLayer(float2 texel)
{
    return DepthOn() && gUser[5].y > 0.0 && DepthBehind(texel) > gUser[5].y * 1.8;
}

// 深度の段差：上下左右 width texel の標本が gap 以上手前にあるか（物の外側、奥の面の側）。
bool DepthStep(float2 texel, float width, float gap)
{
    if (!DepthOn())
        return false;
    const float center = DepthBehind(texel);
    const float2 offsets[4] = {float2(width, 0), float2(-width, 0), float2(0, width), float2(0, -width)};
    [unroll] for (int i = 0; i < 4; ++i)
    {
        if (center - DepthBehind(texel + offsets[i]) > gap)
            return true;
    }
    return false;
}

// 深度の線：段差が depthLineGap 以上の所。0..depthLine。奥の層（窓の向こう）には引かない（奥の光や柱が管のように縁取られる）。
float DepthEdge(float2 texel, float width)
{
    if (gUser[7].z <= 0.0 || FarLayer(texel))
        return 0.0;
    return DepthStep(texel, width, max(gUser[9].z, 1e-3)) ? gUser[7].z : 0.0;
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
    const float luminance = HikariLuminance(color);
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
        const float2 source = (cell - round(offset * ((float)step / (float)steps)) + 0.5) * block;
        if (ObjectAt(source, ReadTexel(source)))
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
    // 深度がある時は物の高さに応じた光の影（外周の市松は使わない）。
    if (DepthOn() && gUser[7].w > 0.0)
        return LightShadow((cell + 0.5) * block, block) > 0.0 ? 1.0 : 0.0;
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
    const float2 center = (cell + 0.5) * block;
    const float3 color = ReadTexel(center);
    const bool checker = fmod(cell.x + cell.y, 2.0) < 0.5;
    if (ObjectAt(center, color))
    {
        const float3 shaded = IsGhost(color) && fmod(cell.y, 2.0) >= 0.5 ? color * 0.55 : color;
        // 深度：入隅と影は一段暗く（ドット絵の一段。市松で混ぜると縁が毛羽立つ）
        const float dark = Occlusion(center, block) * 0.6 + LightShadow(center, block) * 0.5;
        return shaded * (1.0 - (dark > 0.25 ? 0.28 : 0.0));
    }
    const float3 outline = StyleLinear(gUser[1].rgb);
    const int reach = (int)round(gUser[0].z);
    [loop] for (int distance = 1; distance <= reach; ++distance)
    {
        const float2 offsets[4] = {float2(distance, 0), float2(-distance, 0), float2(0, distance),
                                   float2(0, -distance)};
        [unroll] for (int i = 0; i < 4; ++i)
        {
            const float2 neighbor = (cell + offsets[i] + 0.5) * block;
            if (ObjectAt(neighbor, ReadTexel(neighbor)))
                return outline;
        }
    }
    const float shadow = CastShadow(cell, block);
    // 深度がある時の影は面の色を一段暗くする（明るい壁に輪郭の色の真っ黒な影を落とさない）。
    float3 result = DepthOn() ? (shadow >= 1.0 ? color * (1.0 - 0.42 * gUser[0].w) : color)
        : shadow >= 1.0 || (shadow > 0.0 && checker) ? lerp(color, outline, gUser[0].w) : color;
    // 深度：奥ほど霧の色へ（三段を市松で繋ぐ）、物の周りの奥の面は暗く。
    if (DepthOn())
    {
        result = lerp(result, StyleLinear(gUser[6].rgb), FogAmount(center));
        result *= 1.0 - (Occlusion(center, block) > 0.25 ? 0.3 : 0.0) * gUser[0].w;
    }
    const float4 glow = NearestVivid((cell + 0.5) * block, block, reach + 2);
    if (glow.a > reach + 1.5)
        return DepthOn() ? lerp(result, glow.rgb * 0.75, 0.12) : checker ? lerp(result, glow.rgb * 0.75, 0.22) : result;
    if (glow.a > 0.0)
        return lerp(result, glow.rgb * 0.75, 0.35);
    return result;
}

// ---------------------------------------------------------------- Sketch
// 鉛筆線の種類：0 無し、1 物体の中で焼いた色の段が変わる所（右と下の隣だけと比べて片側に 1 本）、
// 2 物体の外形（背景の側に 1 本）。両側に引くと線が倍の太さになり、細い足場の上面が塗り潰されて見える。
// neighbor は外形の時に接している物体の色（鮮やかな物体は線をその色の色鉛筆で引くため）。
// lit は外形の線が物体の左か上の縁（光の当たる側、物体がこの texel の右か下にある）か。
int SketchEdge(float2 position, float3 color, float distance, out float3 neighbor, out bool lit)
{
    neighbor = 0.0;
    lit = false;
    const bool object = ObjectAt(position, color);
    const float2 offsets[4] = {float2(distance, 0), float2(0, distance), float2(-distance, 0), float2(0, -distance)};
    int edge = 0;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        const float3 other = ReadTexel(position + offsets[i]);
        const bool otherObject = ObjectAt(position + offsets[i], other);
        if (!object && otherObject)
        {
            neighbor = other;
            lit = i < 2;
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
        const float2 probe = position + directions[i] * 2.0;
        if (ObjectAt(probe, ReadTexel(probe)))
            return 0.16;
    }
    [unroll] for (int j = 0; j < 8; ++j)
    {
        const float2 probe = position + directions[j] * 5.0;
        if (ObjectAt(probe, ReadTexel(probe)))
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

// 鮮やかな物体（人物・残影・仕掛けの意味の色）の外形の線：物体の外側の四方を、その色を濃くした色鉛筆で引く。
// 地形の白墨の線より太く、太さは画面の上でゆっくり揺らす（筆圧）。戻り値は線の濃さ（0 なら線なし）。
// 探すのは縦横と斜めの 8 方向だけ（全部の texel を見ると重い）。
float SketchVividLine(float2 position, float2 texel, float block, out float3 lineColor)
{
    lineColor = 0.0;
    const float2 cell = texel / 5.0;
    const float2 base = floor(cell);
    const float2 f = smoothstep(0.0, 1.0, frac(cell));
    const float pressure = lerp(lerp(HikariHash12(base), HikariHash12(base + float2(1, 0)), f.x),
                                lerp(HikariHash12(base + float2(0, 1)), HikariHash12(base + float2(1, 1)), f.x), f.y);
    const int reach = max((int)round(lerp(1.0, 2.2, pressure) * block), 1);
    const float2 directions[8] = {float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
                                  float2(0.7071, 0.7071), float2(-0.7071, 0.7071), float2(0.7071, -0.7071), float2(-0.7071, -0.7071)};
    [loop] for (int distance = 1; distance <= reach; ++distance)
    {
        [unroll] for (int i = 0; i < 8; ++i)
        {
            const float3 other = ReadTexel(position + directions[i] * distance);
            if (IsVivid(other))
            {
                // 濃い色：sRGB で暗くして色相は残す。外側ほど少し淡く（線の縁が柔らかい）。
                lineColor = StyleSrgb(other) * 0.48;
                return lerp(0.95, 0.7, saturate((distance - 1.0) / reach));
            }
        }
    }
    return 0.0;
}

float3 StyleSketch(float2 position)
{
    const float block = max(gUser[0].y, 1.0);
    const float depth = saturate(gUser[0].w);
    const float3 paper = gUser[2].rgb;
    const float3 pencil = gUser[1].rgb;
    const float3 color = ReadTexel(position);
    const float2 texel = floor(position);
    const bool object = ObjectAt(position, color);
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
        const bool lightSurface = HikariLuminance(color) > 0.35 && StyleChroma(tone) < 0.2;
        // 塗りの濃さ（既定 0.32）。明るい天板は 0.23 濃く塗る。濃くしても暗い段は紙より沈め、斜線で形を読ませる。
        const float fill = saturate(gUser[4].z);
        result = lerp(result, tone, lightSurface ? saturate(fill + 0.23) : fill);
        if (IsShadowBand(color))
        {
            const float3 shade = lerp(paper * 0.82 + tone * 0.10, tone * 0.85, saturate((fill - 0.32) / 0.68));
            result = lerp(result, shade, depth);
            if (fmod(texel.x + texel.y, 4.0) < 0.5)
                result = lerp(result, pencil, 0.45 - 0.13 * depth);
        }
        // 深度：入隅は紙へ沈め、光の影は疎らな斜線で（塗りは残して形を読ませる）。
        if (DepthOn())
        {
            result = lerp(result, paper * 0.7, 0.55 * Occlusion(position, block));
            if (LightShadowStep(position, block, 1.0) > 0.0)
            {
                result = lerp(result, paper * 0.8, 0.3 * depth);
                if (fmod(texel.x - texel.y + 4096.0, 5.0) < 0.5)
                    result = lerp(result, pencil, 0.3);
            }
        }
    }
    else if (DepthOn())
    {
        // 背景（深度）：奥の物は霧を掛けた色の明るさを白墨の淡い塗りにする（暗い影絵は紙より沈め、霞と灯は白く）。
        // 奥の物どうしの段差（柱と奥の壁など）に薄い鉛筆の線。物の周りの奥の面は遮蔽で暗く、光の影は一段暗く疎らな斜線。
        const float3 hazed = lerp(StyleSrgb(color), gUser[6].rgb, FogAmount(position));
        const float value = dot(hazed, float3(0.2126, 0.7152, 0.0722));
        result = lerp(result * 0.72, pencil, 0.62 * saturate((value - 0.06) * 2.0));
        const bool far = FarLayer(position);
        if (!far && DepthStep(position, 1.0, max(gUser[9].z, 1e-3) * 3.0) && HikariHash12(texel + 29.0) < 0.85)
            result = lerp(result, pencil, 0.22 * depth);
        if (!far && LightShadowStep(position, block, 1.0) > 0.0)
        {
            result *= 1.0 - 0.32 * depth;
            if (fmod(texel.x - texel.y + 4096.0, 6.0) < 0.5)
                result = lerp(result, pencil, 0.08 * depth);
        }
        result *= 1.0 - 0.6 * Occlusion(position, block);
        const float4 glow = NearestVivid(position, 1.0, 4);
        if (glow.a > 0.0)
            result = lerp(result, StyleSrgb(glow.rgb), 0.28 * (1.0 - (glow.a - 1.0) / 4.0));
    }
    else
    {
        // 背景：光の輪が明るい所ほど白墨を淡く掃く（空気の層）。落ち影は一段暗く疎らな斜線、物体の周りは接地の暗さ。
        const float lift = saturate(HikariLuminance(color) / max(gUser[3].w, 1e-4) * 1.2 - 0.35);
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
    // 鮮やかな物体の外形は地形の線より先に引く（人物・仕掛けを地形の白墨の線と分ける）。
    if (gUser[0].z > 0.0 && !vivid)
    {
        float3 vividLine;
        const float strength = SketchVividLine(position, texel, block, vividLine);
        if (strength > 0.0 && HikariHash12(texel + 41.0) < 0.94)
            return StyleLinear(saturate(lerp(result, vividLine, strength)));
    }
    if (gUser[0].z > 0.0)
    {
        // 深度がある時：外形は光の当たる側（物体の左と上の縁）を白墨、影の側（右と下）を暗い墨で引き、物体の中の色の段の
        // 境も暗い墨にする（全部を白墨で囲むと面の明暗が消え、物体が平らな貼り絵に見える）。鮮やかな物体の外形は色鉛筆のまま。
        const bool shaded = DepthOn();
        const float3 ink = paper * 0.35;
        const float distance = max(round(gUser[0].z * block * 0.5), 1.0);
        float3 neighbor;
        bool lit;
        const int edge = SketchEdge(position, color, distance, neighbor, lit);
        const float3 outer = !shaded || lit || IsVivid(neighbor) ? SketchInk(pencil, neighbor) : ink;
        if (edge == 2 || (edge == 1 && !vivid))
        {
            if (HikariHash12(texel + 17.0) < 0.93)
            {
                if (edge == 2)
                    result = lerp(result, outer, 0.9);
                else
                    result = shaded ? lerp(result, ink, 0.45) : lerp(result, SketchInk(pencil, neighbor), lerp(0.85, 0.72, depth));
            }
        }
        else if (!object && depth > 0.0 && SketchEdge(position, color, distance + 1.0, neighbor, lit) == 2)
        {
            const float3 thick = !shaded || lit || IsVivid(neighbor) ? SketchInk(pencil, neighbor) : ink;
            result = lerp(result, thick, 0.42 * depth);   // 外形をもう 1 texel 太く
        }
        else
        {
            const float2 echo = position - 1.0;
            if (HikariHash12(texel + 3.0) < 0.5 && SketchEdge(echo, ReadTexel(echo), distance, neighbor, lit) == 2)
                result = lerp(result, !shaded || lit || IsVivid(neighbor) ? SketchInk(pencil, neighbor) : ink, 0.4);
        }
    }
    return StyleLinear(saturate(result));
}

// ---------------------------------------------------------------- Clean
// 3D 描画をそのまま。深度がある時は、入隅と光の影で暗くし、奥を霧の色へ寄せ、物の外側に lineColor の線を引く。
float3 StyleClean(float2 position)
{
    float3 color = ReadTexel(position);
    if (!DepthOn())
        return color;
    const float block = max(gUser[0].y, 1.0);
    // 奥の層はゲーム画素ひとつ分ぼかす（焦点を遊びの面に置く。奥の層どうしの標本だけを混ぜ、手前の物の縁は滲ませない）。
    if (FarLayer(position))
    {
        float3 sum = color;
        float weight = 1.0;
        [unroll] for (int dy = -1; dy <= 1; ++dy)
        {
            [unroll] for (int dx = -1; dx <= 1; ++dx)
            {
                const float2 tap = position + float2(dx, dy) * block;
                if ((dx != 0 || dy != 0) && FarLayer(tap))
                {
                    sum += ReadTexel(tap);
                    weight += 1.0;
                }
            }
        }
        color = sum / weight;
    }
    color *= (1.0 - 0.4 * Occlusion(position, block)) * (1.0 - 0.45 * LightShadow(position, block));
    color = lerp(color, StyleLinear(gUser[6].rgb), FogAmount(position));
    const float edge = DepthEdge(position, max(round(gUser[0].z * block * 0.5), 1.0));
    return lerp(color, StyleLinear(gUser[1].rgb), edge);
}

// 背景を透かす時の覆い（版 5）：物体と、その外側 lineWidth ゲーム画素以内（輪郭・鉛筆の外形線）だけ 1。
// Pixel はゲーム画素の升目ごとに決める（縁が段々になる）。
float StyleCoverage(uint style, float2 position)
{
    const float block = max(round(gUser[0].y), 1.0);
    const bool grid = style == kStylePixel;
    const float2 cell = grid ? floor(position / block) : position;
    const float2 at = grid ? (cell + 0.5) * block : position;
    if (ObjectAt(at, ReadTexel(at)))
        return 1.0;
    const int reach = max((int)round(gUser[0].z), 1);
    const float step = grid ? block : max(block * 0.5, 1.0);
    [loop] for (int distance = 1; distance <= reach; ++distance)
    {
        const float2 offsets[4] = {float2(distance, 0), float2(-distance, 0), float2(0, distance), float2(0, -distance)};
        [unroll] for (int i = 0; i < 4; ++i)
        {
            const float2 tap = at + offsets[i] * step;
            if (ObjectAt(tap, ReadTexel(tap)))
                return 1.0;
        }
    }
    return 0.0;
}

// 色調（版 6）：色の付いた物体（人物・仕掛け・危険）と、鮮やかな色の縁（2 texel 以内）の外の画素だけを、表示 sRGB で
// 明るさ・明暗の差・彩度・色相・染めの順に調える。既定（何も変えない）なら飛ばす。
bool ToneOn()
{
    return gUser[10].w > 0.0 || abs(gUser[11].x) > 1e-4 || abs(gUser[11].y - 1.0) > 1e-4 || abs(gUser[11].z - 1.0) > 1e-4 ||
           abs(gUser[11].w - 1.0) > 1e-4;
}

float3 ApplyTone(float3 linearColor)
{
    float3 c = StyleSrgb(linearColor) * gUser[11].z;
    c = (c - 0.5) * gUser[11].w + 0.5;
    const float3 weights = float3(0.2126, 0.7152, 0.0722);
    c = lerp(dot(c, weights).xxx, c, gUser[11].y);
    // 灰の軸まわりに色相を回す（Rodrigues の回転）。
    const float angle = gUser[11].x * 6.28318530718;
    const float cosA = cos(angle), sinA = sin(angle);
    const float3 k = float3(0.57735027, 0.57735027, 0.57735027);
    c = c * cosA + cross(k, c) * sinA + k * dot(k, c) * (1.0 - cosA);
    // 染め：明るさを保ったまま tint の色相へ寄せる。
    const float luminance = max(dot(c, weights), 0.0);
    const float3 tint = saturate(gUser[10].rgb);
    const float3 tinted = tint * (luminance / max(dot(tint, weights), 1e-3));
    c = lerp(c, tinted, saturate(gUser[10].w));
    return StyleLinear(saturate(c));
}

// 色の付いた物体（鮮やかな物体より低い彩度 0.18 まで。人物の陰の側も含む）とその縁は調えない。地形・壁・背景の
// 青瓷はほぼ灰色（彩度 0.12 以下）なので対象に残る。
bool ToneTarget(float2 position)
{
    const float3 color = ReadTexel(position);
    if (IsObject(color) && StyleChroma(StyleSrgb(color)) >= 0.18)
        return false;
    return NearestVivid(position, 1.0, 2).a <= 0.0;
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
        result = StyleClean(position);
    if (ToneOn() && ToneTarget(position))
        result = ApplyTone(result);
    const float alpha = gUser[9].w > 0.5 ? StyleCoverage(style, position) : 1.0;
    return float4(result * gUser[1].w, alpha);
}
