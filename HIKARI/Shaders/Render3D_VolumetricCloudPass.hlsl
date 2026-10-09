#include "Include/Sky/HIKARI_VolumetricCloud.hlsli"
#include "Include/Lighting/HIKARI_CloudSolarVisibility.hlsli"
#include "Include/Atmosphere/HIKARI_SkyViewMapping.hlsli"

// 体積雲の描画。半分の解像度の 2x2 ごとに 1 画素だけを毎フレーム場所を変えて追跡し（TraceCloudPS）、
// 前フレームの履歴と合わせて半分の解像度へ再構成し（ReprojectCloudPS）、全解像度の場面へ深度を見て合成する
// （CompositeCloudPS）。雲の色は Pre-Exposure 済みの乗算済み輝度、alpha は
// 1 - 透過率。深度は x 最初に当たった距離、y 平均距離 [m]。
// SunTransmittanceCS は地表へ落とす雲の影の表を、TraceCloudSkyCS は環境光へ渡す雲の空（天空 LUT と同じ写像、
// 絶対輝度）を作る。地平線より下（天空 LUT の背景）には何も描かない。

Texture2D<float4> gCloudCurrentColor : register(t3);
Texture2D<float2> gCloudCurrentDepth : register(t4);
Texture2D<float4> gCloudHistoryColor : register(t5);
Texture2D<float> gCloudSceneDepth : register(t6);
Texture3D<float4> gCloudAerialPerspective : register(t7);
Texture2D<float4> gCloudSkyView : register(t8);
SamplerState gCloudPointClamp : register(s2);

static const float HIKARI_CLOUD_NO_HIT = 1.0e7f;
// 雲の外を探す粗い歩幅の最小 [m]。
static const float HIKARI_CLOUD_COARSE_STEP_MIN = 100.0f;

struct HikariCloudFullscreen
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct HikariCloudTarget
{
    float4 color : SV_Target0;
    float2 depth : SV_Target1;
};

HikariCloudFullscreen FullscreenVS(uint vertexId : SV_VertexID)
{
    HikariCloudFullscreen output;
    output.uv = float2((vertexId << 1u) & 2u, vertexId & 2u);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float3 HikariCloudWorldPoint(float2 uv, float depth)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    const float4 world = mul(gCloudInverseViewProj, clip);
    return world.xyz / world.w;
}

float3 HikariCloudViewRay(float2 uv)
{
    return normalize(HikariCloudWorldPoint(uv, 1.0f) - HikariCloudWorldPoint(uv, 0.0f));
}

// 半解像度の画素が覆う 4 画素のうち最も遠い場面の距離（空なら無限）。合成で画素ごとに手前の物体を除く。
float HikariCloudSceneDistance(float2 uv)
{
    const float4 depths = gCloudSceneDepth.GatherRed(gCloudPointClamp, uv);
    const float depth = max(max(depths.x, depths.y), max(depths.z, depths.w));
    if (depth >= 0.999999f)
    {
        return HIKARI_CLOUD_NO_HIT;
    }
    return length(HikariCloudWorldPoint(uv, depth) - gCloudCamera.xyz);
}

// 空からの環境光（天頂と仰角 55 度の 4 方位の平均。余弦の重みに近く、地平線際の夕焼けの光環を拾いすぎない）。
// 天空 LUT は絶対輝度。大気の空でなければ CPU の概算。
float3 HikariCloudSkyAmbient()
{
    if (gCloudAerial.z < 0.5f)
    {
        return gCloudFallbackAmbient.rgb;
    }
    float3 sum = gCloudSkyView.SampleLevel(gCloudLinearClamp, HikariAtmosphereSkyViewUv(float3(0.0f, 1.0f, 0.0f)),
        0.0f).rgb * 2.0f;
    [unroll]
    for (uint index = 0u; index < 4u; ++index)
    {
        const float azimuth = float(index) * 1.57079633f;
        const float3 direction = float3(cos(azimuth) * 0.574f, 0.819f, sin(azimuth) * 0.574f);
        sum += gCloudSkyView.SampleLevel(gCloudLinearClamp, HikariAtmosphereSkyViewUv(direction), 0.0f).rgb;
    }
    return sum / 6.0f;
}

// 視線 direction の手前側（カメラの側）の空の光。視線の後ろ側の仰角 20 度と 50 度の空の平均。見えている雲の面は
// この空に照らされる（夕方に太陽の方を見ると陰の面は東の青紫の空に、反対を見ると西の夕焼けの空に照らされる）。
float3 HikariCloudSkyAmbientFacing(float3 direction)
{
    const float horizontal = length(direction.xz);
    if (gCloudAerial.z < 0.5f || horizontal < 1.0e-3f)
    {
        return HikariCloudSkyAmbient();
    }
    const float2 back = -direction.xz / horizontal;
    const float3 low = float3(back.x * 0.940f, 0.342f, back.y * 0.940f);
    const float3 high = float3(back.x * 0.643f, 0.766f, back.y * 0.643f);
    return 0.5f * (gCloudSkyView.SampleLevel(gCloudLinearClamp, HikariAtmosphereSkyViewUv(low), 0.0f).rgb +
        gCloudSkyView.SampleLevel(gCloudLinearClamp, HikariAtmosphereSkyViewUv(high), 0.0f).rgb);
}

// 歩みの開始位置のずらし。追跡の画素ごとの Interleaved Gradient Noise に、履歴の画素の更新（4 フレームに 1 回）
// ごとに黄金比ずつ足し、同じ 2x2 の 4 画素は 1/4 ずつずらす。履歴の各画素のずらしが更新をまたいで一様に巡り、
// 時間方向に混ぜると早く収束して模様が残らない。
float HikariCloudInterleavedGradientNoise(float2 pixel)
{
    const uint frame = uint(gCloudTemporal.x);
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))) + 0.25f * float(frame & 3u) +
        0.61803399f * float((frame >> 2u) & 255u));
}

// 方向 direction の空の輝度 [nits]（太陽円盤なし）。
float3 HikariCloudSkyRadiance(float3 direction)
{
    if (gCloudAerial.z < 0.5f)
    {
        return gCloudFallbackAmbient.rgb;
    }
    return gCloudSkyView.SampleLevel(gCloudLinearClamp, HikariAtmosphereSkyViewUv(direction), 0.0f).rgb;
}

// 指数大気で高さ h0 から h1 へ距離 distance の直線を通る時の、密度の積分 [m]（地表の密度を 1 とする）。
// 球面の曲がりは両端の実際の高さで表す。
float HikariCloudAirColumn(float h0, float h1, float distance, float scaleHeight)
{
    const float low = exp(-max(h0, 0.0f) / scaleHeight);
    const float rise = h1 - h0;
    if (abs(rise) < 0.01f * scaleHeight)
    {
        return low * distance;
    }
    return (low - exp(-max(h1, 0.0f) / scaleHeight)) * scaleHeight * distance / rise;
}

// 雲までの大気の透過率と、その手前で散乱して目に届く光。手前の光は、その方向の空の輝度のうち雲までの区間が
// 占める割合（1 - T(d)）/（1 - T(空の果て)）で表す。天空 LUT の色（太陽まわりの光環や夕焼け）をそのまま使える。
// 雲の陰になった空気は主光源の代わりに雲の下の空の光で照らされる。
void HikariCloudAirPerspective(float3 direction, float altitude, float distance, float3 skyRadiance,
    out float3 transmittance, out float3 inscatter)
{
    const float h0 = gCloudCamera.y;
    const float3 depth = gCloudAirRayleigh.rgb * HikariCloudAirColumn(h0, altitude, distance, gCloudAirRayleigh.w) +
        gCloudAirMie.x * HikariCloudAirColumn(h0, altitude, distance, gCloudAirMie.y);
    // 空の果てまでの光学的厚さ。仰角の低い方向は Kasten-Young のエアマスで伸ばす。
    const float elevation = degrees(asin(clamp(direction.y, -1.0f, 1.0f)));
    const float airMass = 1.0f / max(max(direction.y, 0.0f) + 0.50572f * pow(max(elevation, 0.0f) + 6.07995f,
        -1.6364f), 1.0e-3f);
    const float3 total = (gCloudAirRayleigh.rgb * gCloudAirRayleigh.w * exp(-max(h0, 0.0f) / gCloudAirRayleigh.w) +
        gCloudAirMie.x * gCloudAirMie.y * exp(-max(h0, 0.0f) / gCloudAirMie.y)) * airMass;
    transmittance = exp(-depth);
    const float visibility = gCloudAirUnderClouds.w;
    inscatter = (skyRadiance * visibility / max(1.0f - exp(-max(total, depth)), 1.0e-3f) +
        gCloudAirUnderClouds.rgb * (1.0f - visibility)) * (1.0f - transmittance);
}

// 雲の 1 点から視線方向へ出る光 [nits]（アルベド込み）。
// 雲の中の落雷の光が position に作る散乱光 [nits]。光度 I の点光源の近くの照度（数百 m で 1/d 程度へ緩む）と、
// 雲の層の中を何度も散乱して数 km に広がった光（3 km の幅で 1/(d^2 + 3km^2)、雲底のどこからでも見える閃き）を
// 等方に散らす。雷雲の内側が青白く光り、落雷が視点の後ろでも前方の雲底が明るむ。
float3 HikariCloudLightningRadiance(float3 position)
{
    if (gCloudLightning.w <= 0.0f)
    {
        return 0.0f.xxx;
    }
    const float distance = length(position - gCloudLightning.xyz);
    const float nearby = 1.0f / max(distance * (distance + 300.0f), 1.0e4f);
    const float spread = 3.0f / (distance * distance + 9.0e6f);
    // 色は HIKARI_Lightning.cpp の落雷の光と同じ青白。
    return float3(0.85f, 0.9f, 1.0f) * gCloudLightning.w * (nearby + spread) / (4.0f * HIKARI_CLOUD_PI);
}

// localVisibility は塊の凹凸による空と地面の光の遮り（HikariCloudLocalVisibility、遠方は 1）。何度も散乱した光が
// くぼみにも届くので 3 割は残す。skyFacing は視線の手前側の空の光、viewDepth は視線が雲に入ってからの光学的厚さ。
float3 HikariCloudSourceRadiance(HikariCloudColumn column, float cosine, float lightDepth, float density,
    float3 lightRadiance, float3 skyAmbient, float3 skyFacing, float viewDepth, float3 groundAmbient,
    float2 localVisibility)
{
    // 雲が横に連なっている度合い。被覆率が 1 に近いほど一面の雲（HikariCloudMergeCell と同じ c^4）。
    const float deck = column.coverage * column.coverage * column.coverage * column.coverage;
    // 雲底（上が雲で下が開いている面。localVisibility で見分ける）には、上から入った主光源が雲の中を拡散して届く
    // （μ / π / (1 + 0.11 τ μ) の半分。残りは横や上へ抜ける）。一面の雲は HikariCloudLightScattering の板の拡散透過が
    // 持つので deck で引く。光源方向の厚さ τ に細部が入るので、薄い所が明るく厚い所が暗いまだらになり、雲底が一様な
    // 灰色の板にならない。
    const float elevation = max(gCloudLightDirection.y, 0.0f);
    const float bottom = (1.0f - localVisibility.x) * localVisibility.y * (1.0f - deck);
    const float transmitted =
        0.5f * elevation / (HIKARI_CLOUD_PI * (1.0f + 0.11f * lightDepth * max(elevation, 0.1f)));
    const float3 sun = lightRadiance *
        (HikariCloudLightScattering(cosine, lightDepth, density, deck, gCloudLightDirection.y) + transmitted * bottom);
    // 雲の上と下から入る環境光は、この柱でそこまでにある雲の厚さ（平均の濃さ 0.5）を拡散透過 1/(1 + 0.11τ) で
    // 抜けてくる。雲の側面は横の空も見ているので、鉛直の柱で指数的に消すと積雲の中が黒い芯になる。
    const float aboveDepth = gCloudDensity.x * 0.5f * column.above;
    const float belowDepth = gCloudDensity.x * 0.5f * column.below;
    // 空の光の半分は上から柱の上の雲を、残り半分は視線の手前側の開いた空から視線が雲に入ってからの厚さを抜けてくる。
    // 上の柱だけで消すと、見えている雲の表面（陰の面や雲底）まで空の光が届かず、陰が太陽の多重散乱（夕方は橙）だけの
    // 色になる。
    const float2 occlusion = lerp(0.3f.xx, 1.0f.xx, localVisibility);
    const float3 sky = 0.5f * skyAmbient / (1.0f + 0.11f * aboveDepth) + 0.5f * skyFacing / (1.0f + 0.11f * viewDepth);
    const float3 ambient = sky * occlusion.x + groundAmbient * occlusion.y / (1.0f + 0.11f * belowDepth);
    return HIKARI_CLOUD_ALBEDO * (sun + ambient);
}

struct HikariCloudMarch
{
    float3 nearRadiance;     // 最大距離までの雲の光 [nits]（空気遠近は掛けていない）
    float nearTransmittance; // 最大距離までの雲の透過率
    float nearDepth;         // その光の重み付き平均距離 [m]
    float3 farRadiance;      // 最大距離より先の雲の光 [nits]（空気遠近と手前の雲の透過率を掛けた後）
    float transmittance;     // 全体の透過率
    float front;             // 最初に当たった距離 [m]
    float meanDepth;         // 全体の光の重み付き平均距離 [m]
};

// 視線に沿って雲を追跡する。最大距離（gCloudShell.z）までは個々の雲を、その先（地平線近くでは数百 km）は
// 天気の平均の濃さ（HikariCloudStatisticalDensity）の雲として粗く足す。最大距離の最後の 3 割（gCloudShell.w から）で
// 個々の雲を平均へ移し、境目を見せない。statistical なら最初から平均の濃さだけで追跡する（環境光用の雲の空）。
// 雲の外は粗い歩幅で探し、雲に当たったら一歩戻って画素の幅の細い歩幅で進む（雲の縁が数十 m で立ち上がるので、
// 粗いままだと縁が層状に切れる）。
// quality は x 最大歩数、y 光源方向のサンプル数、z 雲の中の最小歩幅 [m]、w 距離あたりの粗い歩幅。
HikariCloudMarch HikariCloudMarchRay(float3 origin, float3 direction, float sceneDistance, float jitter, float4 quality,
    float detailDistance, bool statistical)
{
    HikariCloudMarch march;
    march.nearRadiance = 0.0f.xxx;
    march.nearTransmittance = 1.0f;
    march.nearDepth = HIKARI_CLOUD_NO_HIT;
    march.farRadiance = 0.0f.xxx;
    march.transmittance = 1.0f;
    march.front = HIKARI_CLOUD_NO_HIT;
    march.meanDepth = HIKARI_CLOUD_NO_HIT;
    float start, shellEnd;
    const bool shellHit = HikariCloudShellInterval(origin, direction, start, shellEnd);
    const float cosine = dot(direction, gCloudLightDirection.xyz);
    const float3 lightRadiance = gCloudLightColor.rgb * gCloudLightDirection.w * gCloudLook.x;

    const float3 skyAmbient = HikariCloudSkyAmbient() * gCloudDensity.z;
    const float3 skyFacing = HikariCloudSkyAmbientFacing(direction) * gCloudDensity.z;
    // 地面の照り返し（下からの環境光）。地表の照度 = 雲を抜けた直射 + 雲の下の空。
    const float3 groundIrradiance = gCloudLightColor.rgb * gCloudLightColor.w * max(gCloudLightDirection.y, 0.0f) *
        gCloudAirUnderClouds.w + gCloudAirUnderClouds.rgb * HIKARI_CLOUD_PI;
    const float3 groundAmbient = groundIrradiance * (gCloudDensity.w / HIKARI_CLOUD_PI) * gCloudDensity.z;

    float transmittance = 1.0f;
    float3 radiance = 0.0f.xxx;
    // 近い雲（空気遠近を後で掛ける分）の不透明度と重み付き距離、全体の重み付き距離。
    float nearOpacity = 0.0f;
    float depthSum = 0.0f;
    float depthWeight = 0.0f;
    float meanDepthSum = 0.0f;
    float meanDepthWeight = 0.0f;
    // 雨の幕。雲の殻の下にいる時、カメラから殻の下端か地面（最大 30km）までの降水を雨の柱として先に足す。
    // 雨粒は上の雨雲の底（上からの直射と空の光が柱を拡散透過 1/(1 + 0.11τ) で抜けた輝度）と下の地面の照り返しの
    // 平均と、柱を抜けた主光源の前方散乱で照らされる。雨雲の下の雨は雲底より暗く、雲底の濃淡を隠さない。
    if (gCloudWeather.z > 0.0f && HikariCloudAltitude(origin) < gCloudShell.x)
    {
        float groundNear, groundFar;
        const bool groundHit =
            HikariCloudRaySphere(origin, direction, gCloudCamera.w, groundNear, groundFar) && groundNear > 0.0f;
        const float rainEnd =
            min(min(shellHit ? start : (groundHit ? groundNear : 30000.0f), sceneDistance), 30000.0f);
        const float rainStep = rainEnd / 12.0f;
        const float3 above = lightRadiance * max(gCloudLightDirection.y, 0.0f) / HIKARI_CLOUD_PI + skyAmbient;
        const float forward = HikariCloudHenyeyGreenstein(cosine, 0.6f);
        [loop]
        for (uint rainIndex = 0u; rainIndex < 12u; ++rainIndex)
        {
            const float distance = (float(rainIndex) + jitter) * rainStep;
            const float3 position = origin + direction * distance;
            float cloudDepth;
            const float rain = HikariCloudRainDensity(position, HikariCloudAltitude(position), cloudDepth);
            if (rain <= 0.0f)
            {
                continue;
            }
            const float3 rainSource = HIKARI_CLOUD_ALBEDO *
                (0.5f * (above / (1.0f + 0.11f * cloudDepth) + groundAmbient) +
                lightRadiance * exp(-cloudDepth / max(gCloudLightDirection.y, 0.1f)) * forward);
            const float stepTransmittance = exp(-rain * HIKARI_CLOUD_RAIN_EXTINCTION * rainStep);
            const float weight = transmittance * (1.0f - stepTransmittance);
            radiance += rainSource * weight;
            nearOpacity += weight;
            depthSum += distance * weight;
            depthWeight += weight;
            meanDepthSum += distance * weight;
            meanDepthWeight += weight;
            if (march.front >= HIKARI_CLOUD_NO_HIT && weight > 0.002f)
            {
                march.front = distance;
            }
            transmittance *= stepTransmittance;
        }
    }

    const float end = shellHit ? min(min(shellEnd, sceneDistance), start + 300000.0f) : 0.0f;
    if (!shellHit || end <= start)
    {
        march.nearRadiance = radiance;
        march.nearTransmittance = transmittance;
        march.nearDepth = depthWeight > 0.0f ? depthSum / depthWeight : HIKARI_CLOUD_NO_HIT;
        march.transmittance = transmittance;
        march.meanDepth = march.nearDepth;
        return march;
    }
    const float nearEnd = min(end, start + gCloudShell.z);
    const uint maxSteps = (uint)quality.x;
    const uint lightSamples = (uint)quality.y;
    // 歩幅。雲の外は距離に比例した粗い歩幅（quality.w × 2.5 × 距離、最小 HIKARI_CLOUD_COARSE_STEP_MIN、最大 800m）で探し、
    // 雲に当たったら一歩戻って細い歩幅で進む。細い歩幅は追跡の 1 画素が張る幅（距離 × gCloudAerial.w）を最小 quality.z・
    // 最大 200m に収めたもの。消散 0.1/m なら 1 歩の光学的厚さが 1 前後になり、表面の位置と明るさを数標本で積分する
    // （1 歩で不透明になる歩幅では表面が歩幅の幅でぼけ、明るさは 1 標本の当たり外れで決まる）。薄い所（雲底の
    // 立ち上がりや縁）は直前の標本の濃さに応じて最大 3 倍まで伸ばし、1 歩の光学的厚さをそろえる（雲底を見上げる
    // 視線は 80m の立ち上がりを細い歩幅で 7 歩も刻み、追跡の費用の大半になる）。雲の外の歩幅が長いと、輪廓をかすめる
    // 短い弦を拾ったり外したりして縁が数画素ぼける。細い歩幅のまま粗い歩幅ぶんの空の標本が続いたら粗い歩幅へ戻る。

    float t = start + clamp(quality.w * 2.5f * start, HIKARI_CLOUD_COARSE_STEP_MIN, 800.0f) * jitter;
    bool fine = false;
    uint emptySamples = 0u;
    uint emptyLimit = 0u;
    float lastDensity = 0.0f;
    // 光源方向の光学的厚さと塊の凹凸による遮りは、雲の中で視線に沿って 30m ごとに求めて使い回す（標本ごとに求めると
    // 光源の追跡が追跡全体の費用の大半になる。30m の間の τ の差は雲の奥へ入る分だけで、奥の標本は透過率の重みが
    // 小さい）。
    float lightDepthCache = 0.0f;
    float2 localVisibility = 1.0f.xx;
    float cacheDistance = -1.0e9f;
    // 平均の濃さへ移る遠方（fade > 0）は空気遠近を標本ごとに掛けて far へ足す。近い雲の平均距離の空気遠近を掛けると、
    // 遠方の雲が近い雲と同じだけしか霞まず、地平線に明るい帯が残る。
    const float3 skyRadiance = HikariCloudSkyRadiance(direction);
    float3 far = 0.0f.xxx;
    [loop]
    for (uint index = 0u; index < maxSteps * 3u && t < nearEnd; ++index)
    {
        // 遠方へ移る範囲は平均の濃さなので細かく進まない。
        const float fade = statistical ? 1.0f : smoothstep(gCloudShell.w, gCloudShell.z, t);
        fine = fine && fade <= 0.0f;
        const float coarseStep = clamp(quality.w * 2.5f * t, HIKARI_CLOUD_COARSE_STEP_MIN, 800.0f);
        const float fineStep = clamp(t * gCloudAerial.w, quality.z, 200.0f);
        const float stepLength = fine ? fineStep * clamp(0.35f / max(lastDensity, 0.12f), 1.0f, 3.0f) : coarseStep;
        const float3 position = origin + direction * t;
        const float altitude = HikariCloudAltitude(position);
        // 細部ノイズは detailDistance の手前 4 割で平均へ移し、切り替わりの境目を見せない。
        const float detailWeight = 1.0f - smoothstep(detailDistance * 0.6f, detailDistance, t);
        HikariCloudColumn column = (HikariCloudColumn)0;
        float density = HikariCloudStatisticalDensity(position, altitude, column) * fade;
        if (fade < 1.0f)
        {
            column = HikariCloudResolveColumn(position, altitude);
            density += HikariCloudDensity(column, position, altitude, detailWeight, t * gCloudAerial.w) *
                (1.0f - fade);
        }
        if (density > 0.0f && !fine && fade <= 0.0f)
        {
            // 雲の表面は (t - coarseStep, t] にある。二分探索 2 回で 1/4 の幅に絞ってから細い歩幅で進む（粗い歩幅ぶん
            // 戻って細く進むと空の標本を 8 つほど無駄にする）。区間の始まりより前へは戻らないが、その時も画素ごとの
            // ずらしを残し、平らな雲底を斜めに見た所で標本の位置がそろって網点にならないようにする。
            fine = true;
            emptySamples = 0u;
            float low = max(t - coarseStep, start);
            float high = t;
            lastDensity = density;
            [unroll]
            for (uint split = 0u; split < 2u; ++split)
            {
                const float middle = 0.5f * (low + high);
                const float3 probe = origin + direction * middle;
                const float probeAltitude = HikariCloudAltitude(probe);
                const float probeDensity = HikariCloudDensity(HikariCloudResolveColumn(probe, probeAltitude), probe,
                    probeAltitude, detailWeight, middle * gCloudAerial.w);
                if (probeDensity > 0.0f)
                {
                    high = middle;
                    lastDensity = probeDensity;
                }
                else
                {
                    low = middle;
                }
            }
            emptyLimit = uint(ceil((high - low) / fineStep)) + 2u;
            t = low <= start ? start + fineStep * jitter : low;
            continue;
        }
        emptySamples = density > 0.0f ? 0u : emptySamples + 1u;
        lastDensity = density;
        if (fine && emptySamples >= emptyLimit)
        {
            fine = false;
        }
        if (density > 0.0f)
        {
            const float extinction = HikariCloudExtinction(density);
            // 平均の濃さの範囲は、光源までの厚さも柱の上の雲の平均で見積もる。
            const bool detailed = fade < 0.5f;
            float lightDepth = extinction * column.above / max(gCloudLightDirection.y, 0.1f);
            float2 visibility = 1.0f.xx;
            if (detailed)
            {
                if (t - cacheDistance > 30.0f)
                {
                    lightDepthCache = HikariCloudLightOpticalDepth(position, lightSamples);
                    localVisibility = HikariCloudLocalVisibility(position, detailWeight);
                    cacheDistance = t;
                }
                lightDepth = lightDepthCache;
                visibility = localVisibility;
            }
            const float3 source = HikariCloudSourceRadiance(column, cosine, lightDepth, density, lightRadiance,
                skyAmbient, skyFacing, -log(max(transmittance, 1.0e-4f)), groundAmbient, visibility) +
                HIKARI_CLOUD_ALBEDO * HikariCloudLightningRadiance(position);
            const float stepTransmittance = exp(-extinction * stepLength);
            const float weight = transmittance * (1.0f - stepTransmittance);
            if (fade > 0.0f)
            {
                float3 air, inscatter;
                HikariCloudAirPerspective(direction, altitude, t, skyRadiance, air, inscatter);
                far += weight * fade * (source * air + inscatter);
            }
            const float nearWeight = weight * (1.0f - fade);
            radiance += source * nearWeight;
            nearOpacity += nearWeight;
            depthSum += t * nearWeight;
            depthWeight += nearWeight;
            meanDepthSum += t * weight;
            meanDepthWeight += weight;
            if (march.front >= HIKARI_CLOUD_NO_HIT && weight > 0.002f)
            {
                march.front = t;
            }
            transmittance *= stepTransmittance;
            if (transmittance < 0.015f)
            {
                break;
            }
        }
        // 対流セルの外（天気図で雲が無い所）は粗い歩幅を 1.5 倍に伸ばす。セルは 400m 以上なので見落とさない。
        // セルの中で雲に当たっていない所は半分に縮め、晴天の小さな雲（高さ数百 m）を粗い歩幅で跨がない。
        t += stepLength * (!fine && fade <= 0.0f ? (column.cell <= 0.0f ? 1.5f : 0.5f) : 1.0f);
    }
    march.nearRadiance = radiance;
    march.nearTransmittance = 1.0f - nearOpacity;
    march.nearDepth = depthWeight > 0.0f ? depthSum / depthWeight : HIKARI_CLOUD_NO_HIT;

    // 最大距離より先。平均の濃さの雲を 8 区間で足し、光源までの厚さは雲層の上端までの平均の濃さで見積もる。
    if (transmittance >= 0.015f && end > t)
    {
        const float segment = (end - t) / 8.0f;
        [loop]
        for (uint farIndex = 0u; farIndex < 8u; ++farIndex)
        {
            const float distance = t + (float(farIndex) + 0.5f) * segment;
            const float altitude = HikariCloudAltitude(origin + direction * distance);
            HikariCloudColumn column = (HikariCloudColumn)0;
            const float density = HikariCloudStatisticalDensity(origin + direction * distance, altitude, column);
            if (density <= 0.0f)
            {
                continue;
            }
            const float extinction = HikariCloudExtinction(density);
            const float lightDepth = extinction * column.above / max(gCloudLightDirection.y, 0.1f);
            const float3 source = HikariCloudSourceRadiance(column, cosine, lightDepth, density, lightRadiance,
                skyAmbient, skyFacing, -log(max(transmittance, 1.0e-4f)), groundAmbient, 1.0f.xx);
            float3 air, inscatter;
            HikariCloudAirPerspective(direction, altitude, distance, skyRadiance, air, inscatter);
            const float opacity = 1.0f - exp(-extinction * segment);
            const float weight = transmittance * opacity;
            far += weight * (source * air + inscatter);
            meanDepthSum += distance * weight;
            meanDepthWeight += weight;
            if (march.front >= HIKARI_CLOUD_NO_HIT && weight > 0.002f)
            {
                march.front = distance;
            }
            transmittance *= 1.0f - opacity;
        }
    }
    march.farRadiance = far;
    march.transmittance = transmittance;
    march.meanDepth = meanDepthWeight > 0.0f ? meanDepthSum / meanDepthWeight : HIKARI_CLOUD_NO_HIT;
    return march;
}

// このフレームに追跡する、履歴の 2x2 の中の位置。対角を先に埋める順（Bayer）で 4 フレームで一巡する。
int2 HikariCloudInterleaveOffset()
{
    const uint phase = uint(gCloudTemporal.x) & 3u;
    return int2(((phase + 1u) >> 1u) & 1u, phase & 1u);
}

HikariCloudTarget TraceCloudPS(HikariCloudFullscreen input)
{
    HikariCloudTarget output;
    output.color = 0.0f.xxxx;
    output.depth = HIKARI_CLOUD_NO_HIT.xx;
    const int2 historyPixel = int2(input.position.xy) * 2 + HikariCloudInterleaveOffset();
    const float2 uv = (float2(historyPixel) + 0.5f) * gCloudHistorySize.zw;
    const float3 direction = HikariCloudViewRay(uv);
    const float sceneDistance = HikariCloudSceneDistance(uv);
    const HikariCloudMarch march = HikariCloudMarchRay(gCloudCamera.xyz, direction, sceneDistance,
        HikariCloudInterleavedGradientNoise(input.position.xy), gCloudQuality, gCloudDetailPhase.w, false);
    if (march.transmittance >= 1.0f)
    {
        return output;
    }
    const float exposure = gCloudTypeThickness.w;
    float3 color = march.nearRadiance * exposure;
    const float nearOpacity = 1.0f - march.nearTransmittance;
    if (gCloudAerial.y > 0.5f && nearOpacity > 0.0f)
    {
        // 近い雲までの空気遠近（Pre-Exposure 済み）。雲の手前の大気の散乱光を足し、雲の光は大気で減る。空気遠近の
        // 体積の奥行き（gCloudAerial.x）の手前 1/4 から先は、遠方の雲と同じ解析の空気遠近へ移す（体積の奥で
        // 止めると、その先の雲が近い雲と同じだけしか霞まず、地平線近くの雲が濃く暗く残る）。
        uint width, height, slices;
        gCloudAerialPerspective.GetDimensions(width, height, slices);
        const float w = sqrt(saturate(march.nearDepth / max(gCloudAerial.x, 1.0f)));
        const float4 aerial = gCloudAerialPerspective.SampleLevel(gCloudLinearClamp,
            float3(uv, max(w - 0.5f / float(slices), 0.5f / float(slices))), 0.0f);
        const float3 volume = color * aerial.a + aerial.rgb * nearOpacity;
        const float beyond = smoothstep(0.75f * gCloudAerial.x, gCloudAerial.x, march.nearDepth);
        float3 analytic = volume;
        if (beyond > 0.0f)
        {
            float3 air, inscatter;
            HikariCloudAirPerspective(direction, HikariCloudAltitude(gCloudCamera.xyz + direction * march.nearDepth),
                march.nearDepth, HikariCloudSkyRadiance(direction), air, inscatter);
            analytic = (march.nearRadiance * air + inscatter * nearOpacity) * exposure;
        }
        color = lerp(volume, analytic, beyond);
    }
    color += march.farRadiance * exposure;
    output.color = float4(all(isfinite(color)) ? max(color, 0.0f.xxx) : 0.0f.xxx, 1.0f - march.transmittance);
    output.depth = float2(march.front, march.meanDepth);
    return output;
}

// Catmull-Rom の 9 点を双線形 5 回にまとめた標本（TAA と同じ）。双線形で毎フレーム読み直すと履歴がだんだんぼやけ、
// 半分の解像度を拡大するときも縁が甘くなるので、雲の色はこれで読む。負の輪郭は 0 で切る。
float4 HikariCloudSampleCatmullRom(Texture2D<float4> source, float2 uv, float2 size)
{
    const float2 position = uv * size;
    const float2 center = floor(position - 0.5f) + 0.5f;
    const float2 f = position - center;
    const float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    const float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    const float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    const float2 w3 = f * f * (-0.5f + 0.5f * f);
    const float2 w12 = w1 + w2;
    const float2 texel = 1.0f / size;
    const float2 uv0 = (center - 1.0f) * texel;
    const float2 uv3 = (center + 2.0f) * texel;
    const float2 uv12 = (center + w2 / w12) * texel;
    float4 result = source.SampleLevel(gCloudLinearClamp, float2(uv12.x, uv0.y), 0.0f) * w12.x * w0.y;
    result += source.SampleLevel(gCloudLinearClamp, float2(uv0.x, uv12.y), 0.0f) * w0.x * w12.y;
    result += source.SampleLevel(gCloudLinearClamp, uv12, 0.0f) * w12.x * w12.y;
    result += source.SampleLevel(gCloudLinearClamp, float2(uv3.x, uv12.y), 0.0f) * w3.x * w12.y;
    result += source.SampleLevel(gCloudLinearClamp, float2(uv12.x, uv3.y), 0.0f) * w12.x * w3.y;
    const float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(result / max(weight, 1.0e-4f), 0.0f.xxxx);
}

// 履歴の各画素を再構成する。このフレームに追跡した画素は新しい値と混ぜ、それ以外は前フレームの履歴を
// 雲の平均距離で投影して使う。どちらも近傍の新しい値（追跡の 3x3）の範囲へ収め、動いた雲の残像を残さない。
// 新しい値は 4 フレームに 1 回だが、重みはそのまま w にして混ぜる標本の数を保つ（HDRP も新しい値は 1/17）。
// 光や雲の変化への追従は 4 倍遅くなるが、急な変化は近傍の範囲で抑える。
HikariCloudTarget ReprojectCloudPS(HikariCloudFullscreen input)
{
    HikariCloudTarget output;
    const int2 pixel = int2(input.position.xy);
    const int2 tracePixel = pixel >> 1;
    const int2 traceLast = (int2(gCloudHistorySize.xy) + 1) / 2 - 1;
    const float4 current = gCloudCurrentColor.Load(int3(tracePixel, 0));
    const float2 currentDepth = gCloudCurrentDepth.Load(int3(tracePixel, 0));
    const bool fresh = all((pixel & 1) == HikariCloudInterleaveOffset());
    output.color = current;
    output.depth = currentDepth;
    if (gCloudTemporal.y < 0.5f)
    {
        return output;
    }
    // 雲の平均距離の点を風で戻してから、前フレームの画面へ投影する。雲が無い画素は遠方として扱う。
    const float distance = currentDepth.y < HIKARI_CLOUD_NO_HIT ? currentDepth.y : gCloudShell.z;
    const float3 world = gCloudCamera.xyz + HikariCloudViewRay(input.uv) * distance -
        float3(gCloudWindDelta.x, 0.0f, gCloudWindDelta.y);
    const float4 previousClip = mul(gCloudPreviousViewProj, float4(world, 1.0f));
    if (previousClip.w <= 0.0f)
    {
        return output;
    }
    const float2 previousUv = float2(previousClip.x / previousClip.w * 0.5f + 0.5f,
        0.5f - previousClip.y / previousClip.w * 0.5f);
    if (any(previousUv <= 0.0f) || any(previousUv >= 1.0f))
    {
        return output;
    }
    float4 history = HikariCloudSampleCatmullRom(gCloudHistoryColor, previousUv, gCloudHistorySize.xy);
    history.rgb *= gCloudWindDelta.z;
    float4 low = current;
    float4 high = current;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float4 neighbour =
                gCloudCurrentColor.Load(int3(clamp(tracePixel + int2(x, y), int2(0, 0), traceLast), 0));
            low = min(low, neighbour);
            high = max(high, neighbour);
        }
    }
    const float4 margin = (high - low) * gCloudTemporal.w;
    history = clamp(history, low - margin, high + margin);
    output.color = fresh ? lerp(history, current, gCloudTemporal.z) : history;
    return output;
}

float4 CompositeCloudPS(HikariCloudFullscreen input) : SV_Target
{
    float4 cloud = HikariCloudSampleCatmullRom(gCloudCurrentColor, input.uv, gCloudHistorySize.xy);
    // 地平線のすぐ上の画素。半分の解像度の雲の画素を補間すると、地平線より下（雲が無く alpha 0）の画素が混ざって
    // alpha が 1 を割り、天空 LUT の晴れた地平線の色が 1 画素の線になって透ける。地平線から数画素の帯では、1 行上
    // （地平線より上）の画素をそのまま使う方が不透明ならそれを採る。
    const float3 viewRay = HikariCloudViewRay(input.uv);
    if (viewRay.y > 0.0f && viewRay.y < 4.0f * gCloudAerial.w)
    {
        const int2 texel = int2(input.uv * gCloudHistorySize.xy);
        const float4 above = gCloudCurrentColor.Load(int3(texel.x, max(texel.y - 1, 0), 0));
        if (above.a > cloud.a)
        {
            cloud = above;
        }
    }
    const float depth = gCloudSceneDepth.Load(int3(input.position.xy, 0));
    if (cloud.a <= 0.0f)
    {
        return 0.0f.xxxx;
    }
    const float front = gCloudCurrentDepth.SampleLevel(gCloudPointClamp, input.uv, 0.0f).x;
    if (depth < 0.999999f && length(HikariCloudWorldPoint(input.uv, depth) - gCloudCamera.xyz) < front)
    {
        return 0.0f.xxxx;
    }
    // Catmull-Rom の行き過ぎで不透明度が 1 を超えないようにし、超えた分だけ色も縮める。
    const float opacity = saturate(cloud.a);
    return float4(cloud.rgb * (opacity / cloud.a), opacity);
}

// 地表の雲の影。光源に垂直な面の格子の各列について、殻を光源側から地表側へたどった累積透過率を書く。
cbuffer HikariCloudSolarFieldCB : register(b1)
{
    HikariCloudSolarVisibilityData gCloudSolarField;
};
// 影の表（RG32F）と雲の空（RGBA16F）の出力。
RWTexture2D<float4> gCloudComputeTarget : register(u0);

[numthreads(8, 8, 1)]
void SunTransmittanceCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint resolution = uint(gCloudSolarField.upResolution.w);
    const uint steps = uint(gCloudSolarField.directionSteps.w);
    const uint2 column = threadId.xy;
    if (any(column >= resolution))
    {
        return;
    }
    const float2 plane = (float2(column) + 0.5f) / float(resolution) * 2.0f - 1.0f;
    const float3 origin = gCloudSolarField.originDescriptor.xyz + (plane.x * gCloudSolarField.rightExtent.xyz +
        plane.y * gCloudSolarField.upResolution.xyz) * gCloudSolarField.rightExtent.w;
    const float3 direction = gCloudSolarField.directionSteps.xyz;
    float innerNear, innerFar, outerNear, outerFar;
    const bool outerHit = HikariCloudRaySphere(origin, direction, gCloudCamera.w + gCloudShell.y, outerNear, outerFar);
    const bool innerHit = HikariCloudRaySphere(origin, direction, gCloudCamera.w + gCloudShell.x, innerNear, innerFar);
    const float start = innerHit ? max(innerFar, 0.0f) : max(outerNear, 0.0f);
    const float end = min(outerFar, start + gCloudShell.z);
    const bool valid = outerHit && end > start;
    gCloudComputeTarget[HikariCloudSolarAtlasPixel(column, 0u, resolution)] =
        valid ? float4(start, end, 0.0f, 0.0f) : 0.0f.xxxx;
    // 1 列を 2x2 の副列（±0.6 列）で平均し、列の境目の段差と光源方向の筋を無くす（影の縁が柔らかくなる。
    // UE の雲の影の図を小さな核でぼかすのと同じ）。
    const float cellSize = 2.0f * gCloudSolarField.rightExtent.w / float(resolution);
    float3 origins[4];
    [unroll]
    for (uint sub = 0u; sub < 4u; ++sub)
    {
        const float2 offset = (float2(sub & 1u, sub >> 1u) - 0.5f) * 1.2f * cellSize;
        origins[sub] = origin + gCloudSolarField.rightExtent.xyz * offset.x +
            gCloudSolarField.upResolution.xyz * offset.y;
    }
    const float strength = gCloudLook.w;
    const float stepLength = valid ? (end - start) / float(steps) : 0.0f;
    float4 opticalDepth = 0.0f.xxxx;
    [loop]
    for (uint step = 1u; step <= steps; ++step)
    {
        if (valid)
        {
            [unroll]
            for (uint sub = 0u; sub < 4u; ++sub)
            {
                if (opticalDepth[sub] >= 12.0f)
                {
                    continue;
                }
                [unroll]
                for (uint sampleIndex = 0u; sampleIndex < 2u; ++sampleIndex)
                {
                    const float distance = end - (float(step - 1u) + (float(sampleIndex) + 0.5f) * 0.5f) * stepLength;
                    const float3 position = origins[sub] + direction * distance;
                    const float altitude = HikariCloudAltitude(position);
                    const float density = HikariCloudDensity(HikariCloudResolveColumn(position, altitude), position,
                        altitude, 0.0f, stepLength);
                    opticalDepth[sub] += HikariCloudExtinction(density) * stepLength * 0.5f;
                }
            }
        }
        // 影の濃さ（見た目の設定）: 1 で物理どおりの透過率、小さいほど影の中にも日照が残る。
        const float visibility = 0.25f * dot(exp(-opticalDepth), 1.0f.xxxx);
        gCloudComputeTarget[HikariCloudSolarAtlasPixel(column, step, resolution)] =
            float4(lerp(1.0f, visibility, strength), 0.0f, 0.0f, 0.0f);
    }
}

// 環境光へ渡す雲の空。天空 LUT と同じ写像の各方向を、天気の平均の濃さで追跡し（1 方向 1 本の光線でセルを拾うと
// 反射がまだらになる）、空気遠近込みの絶対輝度（乗算済み）と不透明度を書く。環境光の畳み込みが空の輝度に重ねる。
[numthreads(8, 8, 1)]
void TraceCloudSkyCS(uint3 threadId : SV_DispatchThreadID)
{
    uint width, height;
    gCloudComputeTarget.GetDimensions(width, height);
    if (any(threadId.xy >= uint2(width, height)))
    {
        return;
    }
    const float3 direction = HikariAtmosphereSkyViewDirection((float2(threadId.xy) + 0.5f) / float2(width, height));
    const float4 quality = float4(max(gCloudQuality.x * 0.5f, 16.0f), max(gCloudQuality.y - 2.0f, 2.0f),
        gCloudQuality.z * 2.0f, gCloudQuality.w * 2.0f);
    const HikariCloudMarch march = HikariCloudMarchRay(gCloudCamera.xyz, direction, HIKARI_CLOUD_NO_HIT, 0.5f, quality,
        0.0f, true);
    float3 color = march.farRadiance;
    const float nearOpacity = 1.0f - march.nearTransmittance;
    if (nearOpacity > 0.0f)
    {
        float3 air, inscatter;
        HikariCloudAirPerspective(direction, HikariCloudAltitude(gCloudCamera.xyz + direction * march.nearDepth),
            march.nearDepth, HikariCloudSkyRadiance(direction), air, inscatter);
        color += march.nearRadiance * air + inscatter * nearOpacity;
    }
    // 太陽の向きの薄い縁は前方散乱で非常に明るくなる。半精度に収め、環境光の畳み込みを乱さない。
    color = all(isfinite(color)) ? min(max(color, 0.0f.xxx), 60000.0f.xxx) : 0.0f.xxx;
    gCloudComputeTarget[threadId.xy] = float4(color, 1.0f - march.transmittance);
}
