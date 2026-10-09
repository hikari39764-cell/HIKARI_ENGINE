#ifndef HIKARI_VOLUMETRIC_CLOUD_INCLUDED
#define HIKARI_VOLUMETRIC_CLOUD_INCLUDED

// 体積雲の共有定義。雲は地表から数 km の殻（カメラ中心の惑星）にあり、被覆率・雲型・降水は天気が、
// 風で流れた位相は環境ランタイムが毎フレーム与える。距離はメートル、輝度は絶対量 [nits]。
// Pre-Exposure は追跡結果を書き出す時だけ掛ける。

cbuffer VolumetricCloudCB : register(b0)
{
    float4x4 gCloudInverseViewProj;
    float4x4 gCloudPreviousViewProj;
    float4 gCloudCamera;          // xyz カメラ位置（y は地表からの高さ）、w 惑星半径 [m]
    float4 gCloudHistorySize;     // 時間方向に再構成する解像度と逆数（追跡はその 2x2 ごとに 1 画素）
    float4 gCloudShell;           // x 殻の下端高度、y 上端高度、z 最大距離、w 遠方の薄れ始め [m]
    float4 gCloudTypeBase;        // 層雲・積雲・積乱雲の雲底 [m]、w 降水で下がる雲底 [m]
    float4 gCloudTypeThickness;   // 同じく厚さ [m]、w このビューの Pre-Exposure
    float4 gCloudWeather;         // 被覆率、雲型（0 層雲 .5 積雲 1 積乱雲）、降水（w は未使用）
    float4 gCloudDensity;         // x 基準の消散係数 [1/m]、y 細部の侵食、z 環境光の倍率、w 地表の反射率
    float4 gCloudShapePhase;      // xy 形状ノイズの位相 [m]、z 周期 [m]、w 発達による上昇 [m]
    float4 gCloudDetailPhase;     // xy 細部ノイズの位相 [m]、z 周期 [m]、w 細部を使う距離 [m]
    float4 gCloudWeatherPhase;    // xy 天気図の位相 [m]、z 周期 [m]
    float4 gCloudLightDirection;  // xyz 主光源（太陽か月）へ向かう方向、w 雲層での照度 [lux]
    float4 gCloudLightColor;      // rgb 雲層での光の色（大気の透過率込み）、w 地表での照度 [lux]
    float4 gCloudTemporal;        // x フレーム番号、y 履歴あり、z 現在フレームの重み、w 近傍の許容幅
    float4 gCloudQuality;         // x 最大歩数、y 光源方向のサンプル数、z 最小歩幅 [m]、w 距離あたりの歩幅
    float4 gCloudAerial;          // x 空気遠近の最大距離 [m]、y 1 なら空気遠近を掛ける、z 1 なら天空 LUT がある、
                                  // w 追跡の 1 画素が張る角度 [rad]
    float4 gCloudWindDelta;       // xy 前フレームからの風の移動（ワールド xz）[m]、z 履歴の P/P_prev
    float4 gCloudFallbackAmbient; // rgb 天空 LUT が無い時の空の輝度の概算 [nits]
    float4 gCloudAirRayleigh;     // rgb 地表の Rayleigh 散乱係数 [1/m]、w スケールハイト [m]
    float4 gCloudAirMie;          // x 地表の Mie 消散係数 [1/m]、y スケールハイト [m]
    float4 gCloudAirUnderClouds;  // rgb 雲の下から見上げた空の平均輝度 [nits]、w 雲を抜けて空気へ届く主光源の割合
    float4 gCloudLightning;       // xyz 落雷の光のワールド位置 [m]、w 光度 [cd]（0 なら無い）
    float4 gCloudLook;            // x 主光源の倍率、y 銀の縁（前方散乱）の倍率、z 雲の群れの強さ、w 置かれた形状に
                                  // 落とす影の濃さ（0..1）
    float4 gCloudWind;            // xy 風の向き（ワールド xz の単位ベクトル）、zw 未使用
};

// ノイズはどれも一様分布（HIKARI_VolumetricCloudNoise.hlsli）。形状と細部は箱フィルタの mip を持つ。
static const float HIKARI_CLOUD_SHAPE_NOISE_SIZE = 128.0f;  // HIKARI_VolumetricCloudStage.cpp の kShapeSize
static const float HIKARI_CLOUD_DETAIL_NOISE_SIZE = 32.0f;  // 同じく kDetailSize
Texture3D<float> gCloudShapeNoise : register(t0);
Texture3D<float> gCloudDetailNoise : register(t1);
Texture2D<float4> gCloudWeatherMap : register(t2);
SamplerState gCloudLinearWrap : register(s0);
SamplerState gCloudLinearClamp : register(s1);

static const float HIKARI_CLOUD_PI = 3.14159265f;
// 雲粒の単一散乱アルベド。
static const float HIKARI_CLOUD_ALBEDO = 0.99f;
// 強い雨（降水 1）の消散係数 [1/m]。視程およそ 4km。
static const float HIKARI_CLOUD_RAIN_EXTINCTION = 1.0e-3f;
// 光源の方向へ影を探す距離 [m]。太陽が低いと数 km〜十数 km 先の雲が影を落とし、照らされる雲がまだらになる。
static const float HIKARI_CLOUD_SHADOW_DISTANCE = 16000.0f;
// 細部ノイズの位置を形状ノイズで横へずらす幅 [m]。縁の凹凸が大きな塊に沿って渦巻き、一様な泡が並ばない。
static const float HIKARI_CLOUD_DETAIL_SWIRL = 48.0f;

float HikariCloudRemap(float value, float low0, float high0, float low1, float high1)
{
    return low1 + (value - low0) * (high1 - low1) / max(high0 - low0, 1.0e-5f);
}

float HikariCloudLerp3(float t, float a, float b, float c)
{
    return t < 0.5f ? lerp(a, b, t * 2.0f) : lerp(b, c, t * 2.0f - 1.0f);
}

// カメラ中心の惑星の中心と、点の地表からの高度。
float3 HikariCloudPlanetCenter()
{
    return float3(gCloudCamera.x, -gCloudCamera.w, gCloudCamera.z);
}

float HikariCloudAltitude(float3 position)
{
    return length(position - HikariCloudPlanetCenter()) - gCloudCamera.w;
}

// 球と半直線の交点（近い方と遠い方）。交わらなければ false。
bool HikariCloudRaySphere(float3 origin, float3 direction, float radius, out float nearDistance, out float farDistance)
{
    const float3 offset = origin - HikariCloudPlanetCenter();
    const float b = dot(offset, direction);
    const float c = dot(offset, offset) - radius * radius;
    const float discriminant = b * b - c;
    nearDistance = 0.0f;
    farDistance = 0.0f;
    if (discriminant < 0.0f)
    {
        return false;
    }
    const float root = sqrt(discriminant);
    nearDistance = -b - root;
    farDistance = -b + root;
    return farDistance > 0.0f;
}

// 視線が雲の殻を通る区間 [start, end]。地面に当たる視線は地面までで止める。
bool HikariCloudShellInterval(float3 origin, float3 direction, out float start, out float end)
{
    const float radius = gCloudCamera.w;
    float innerNear, innerFar, outerNear, outerFar, groundNear, groundFar;
    const bool outerHit = HikariCloudRaySphere(origin, direction, radius + gCloudShell.y, outerNear, outerFar);
    const bool innerHit = HikariCloudRaySphere(origin, direction, radius + gCloudShell.x, innerNear, innerFar);
    const bool groundHit = HikariCloudRaySphere(origin, direction, radius, groundNear, groundFar) && groundNear > 0.0f;
    const float altitude = HikariCloudAltitude(origin);
    start = 0.0f;
    end = 0.0f;
    if (!outerHit)
    {
        return false;
    }
    if (altitude < gCloudShell.x)
    {
        // 殻の下から見上げる。地面へ向かう視線は殻に届かない。
        if (groundHit || !innerHit)
        {
            return false;
        }
        start = innerFar;
        end = outerFar;
    }
    else if (altitude > gCloudShell.y)
    {
        // 殻の上から見下ろす。
        start = max(outerNear, 0.0f);
        end = innerHit && innerNear > 0.0f ? innerNear : outerFar;
    }
    else
    {
        start = 0.0f;
        end = innerHit && innerNear > 0.0f ? innerNear : outerFar;
    }
    return end > start;
}

// 天気図（周期 gCloudWeatherPhase.z の 2D、どれも一様分布）: x 雲域の揺らぎ、y 雲型の揺らぎ、z 対流セル、w 雲頂の塔。
float4 HikariCloudSampleWeather(float2 worldXZ)
{
    return gCloudWeatherMap.SampleLevel(gCloudLinearWrap, (worldXZ + gCloudWeatherPhase.xy) / gCloudWeatherPhase.z,
        0.0f);
}

// 雲頂の塔。セルの中で雲頂を 0.6〜1.35 倍に上下させ、1 つのセルに複数の塔が立つ（丸屋根 1 つにしない）。
float HikariCloudTurret(float4 weather)
{
    return lerp(0.6f, 1.35f, weather.w);
}

// 雲の群れ（雲街）。天気図の雲域の揺らぎを風に沿って 3 倍に伸ばし、2 倍の周期で読む（一様分布。周期 80km なので
// 地平線まで同じ群れは繰り返さない）。
float HikariCloudClusterField(float2 worldXZ)
{
    const float2 wind = gCloudWind.xy;
    const float2 across = float2(-wind.y, wind.x);
    const float2 shifted = worldXZ + gCloudWeatherPhase.xy;
    const float2 stretched = float2(dot(shifted, wind) / 3.0f, dot(shifted, across));
    return gCloudWeatherMap.SampleLevel(gCloudLinearWrap, stretched / (gCloudWeatherPhase.z * 2.0f), 0.0f).x;
}

// その場の被覆率。天気の C の周りで雲域の揺らぎに応じて増減し、C が 0 なら雲は無く、1 なら全天を覆う。
// 被覆率が低い（晴天〜薄曇り）時は雲を風に沿った群れに集め、群れの間を晴らす（群れの中で C の最大 1.85 倍、外で
// 0.15 倍。平均は C のまま）。被覆率が 0.45 を超えると群れは薄れて一面の雲へつながる。
float HikariCloudCoverage(float4 weather, float2 worldXZ)
{
    const float coverage = gCloudWeather.x;
    const float grouping = gCloudLook.z * (1.0f - smoothstep(0.45f, 0.8f, coverage));
    const float cluster = smoothstep(0.2f, 0.8f, HikariCloudClusterField(worldXZ));
    const float gain = lerp(1.0f, lerp(0.15f, 1.85f, cluster), grouping);
    const float spread = 1.2f * sqrt(saturate(coverage * (1.0f - coverage))) * lerp(1.0f, 0.6f, grouping);
    return saturate(coverage * gain + (weather.x - 0.5f) * spread + 0.1f * gCloudWeather.z) *
        smoothstep(0.0f, 0.05f, coverage);
}

float HikariCloudType(float4 weather)
{
    return saturate(gCloudWeather.y + (weather.y - 0.5f) * 0.3f);
}

struct HikariCloudLayer
{
    float base;      // 雲底 [m]
    float thickness; // 雲層の厚さ [m]（被覆率 1 の最も強いセルが届く高さ）
    float baseRise;  // 雲底の立ち上がり [m]。濃さに直接掛け、雲底を凝結高度で平らに切る
    float2 falloff;  // 上端の落ち（f0, f1）を雲の高さ比で
};

HikariCloudLayer HikariCloudResolveLayer(float type)
{
    HikariCloudLayer layer;
    layer.base = HikariCloudLerp3(type, gCloudTypeBase.x, gCloudTypeBase.y, gCloudTypeBase.z) -
        gCloudTypeBase.w * gCloudWeather.z;
    layer.thickness = max(HikariCloudLerp3(type, gCloudTypeThickness.x, gCloudTypeThickness.y, gCloudTypeThickness.z),
        1.0f);
    // 層雲は厚みいっぱい、積雲は上半分で丸く落ち、積乱雲は上の方まで濃い。雲底は遠目には平らで、斜めに見ても
    // 標本が雲の内外で 0/1 に分かれないだけの厚み（積雲 80m、層雲 150m）で立ち上がる。
    layer.baseRise = HikariCloudLerp3(type, 150.0f, 80.0f, 100.0f);
    layer.falloff = float2(HikariCloudLerp3(type, 0.60f, 0.45f, 0.75f), 1.0f);
    return layer;
}

// 雲の高さの尺度 [m]。被覆率が高いほど対流が深い（晴天積雲は数百 m、雄大積雲・積乱雲は数 km）。
float HikariCloudScale(HikariCloudLayer layer, float coverage)
{
    return layer.thickness * lerp(0.08f, 1.0f, coverage * sqrt(coverage));
}

// 被覆率が 1 に近づくとセルの強弱を埋め、全天を覆う一面の雲にする（層雲・曇天）。
float HikariCloudMergeCell(float cell, float coverage)
{
    const float merge = coverage * coverage * coverage * coverage;
    return 1.0f - (1.0f - cell) * (1.0f - merge);
}

// その場の雲（天気図と高さで決まり、3D ノイズは見ない）。天気図の対流セルの強さ g が、その場の被覆率として雲を削る量と
// 雲頂の高さを決める（Horizon Zero Dawn の天気図の被覆率と同じ役割）。雲頂はセルの中心ほど高く（g(2 - g)）、
// 被覆率が高いほど対流が深い（晴天積雲は数百 m、雄大積雲・積乱雲は数 km）。被覆率 1 ではセルがつながって一面になる。
struct HikariCloudColumn
{
    float coverage; // その場の被覆率
    float cell;     // 対流セルの強さ g（0 は雲の外、1 はセルの中心）
    float type;     // 雲型
    float top;      // この柱の雲頂の雲底からの高さ [m]
    float height;   // 雲底からの高さ / top（0 雲底、1 雲頂）
    float profile;  // 雲型ごとの高さ方向の濃さ（上端の落ち）
    float baseFade; // 一番低い雲底からの立ち上がり
    float baseRise; // 雲底の立ち上がりの厚さ [m]
    float above;    // この柱で上にある雲の厚さ [m]
    float below;    // 下にある雲の厚さ [m]
};

// 雲底が形状ノイズで波打つ半幅 [m]。晴天の積雲でも凝結高度のばらつきで 60m 波打ち、雲底が一枚の平らな板に
// ならない。降水する雲は 180m まで広がり、垂れ下がった所が暗い雲底の塊やちぎれ雲になる。
float HikariCloudRaggedBase()
{
    return 60.0f + 120.0f * gCloudWeather.z;
}

// 雲底の縁が細部ノイズでさらに波打つ幅 [m]。形状ノイズの波打ちだけでは、雲底の縁が数百 m の滑らかな曲線で定規で
// 引いたように見える。
static const float HIKARI_CLOUD_DETAIL_RAGGED = 40.0f;

HikariCloudColumn HikariCloudResolveColumn(float3 position, float altitude)
{
    HikariCloudColumn column = (HikariCloudColumn)0;
    const float4 weather = HikariCloudSampleWeather(position.xz);
    column.coverage = HikariCloudCoverage(weather, position.xz);
    if (column.coverage <= 0.0f)
    {
        return column;
    }
    column.type = HikariCloudType(weather);
    // 被覆率が低い所では弱いセル（頂点が 0.12 に届かないもの）を落とし、紙吹雪のような小片を作らない。
    const float cut = 0.12f * (1.0f - smoothstep(0.3f, 0.6f, column.coverage));
    const float cell = saturate((weather.z - (1.0f - column.coverage)) / max(column.coverage, 0.02f));
    column.cell = HikariCloudMergeCell(saturate((cell - cut) / (1.0f - cut)), column.coverage);
    if (column.cell <= 0.0f)
    {
        return column;
    }
    const HikariCloudLayer layer = HikariCloudResolveLayer(column.type);
    column.top = max(HikariCloudScale(layer, column.coverage) * HikariCloudTurret(weather) * column.cell *
        (2.0f - column.cell), 1.0f);
    // 雲底はセルの中心で平らで、縁へ向かってわずかに巻き上がる（雲の高さの 1/10、最大 50m）。どの雲の縁も同じ
    // 薄さで棚のように並ぶことはなく、積雲の雲底は凝結高度でそろった一本の線に見える。
    const float edgeLift = (1.0f - column.cell) * (1.0f - column.cell) * min(column.top * 0.1f, 50.0f);
    // 降水するセルの芯の下は雨で冷えた空気で凝結高度が下がり、雲底が最大 250m 垂れ下がる（暗く低い雷雲の雲底）。
    // 雲底の波打ち（HikariCloudRaggedBase の倍）の一番低い所をこの柱の雲底にし、雲頂の高さはそのまま。
    const float lowered =
        250.0f * gCloudWeather.z * smoothstep(0.5f, 0.9f, column.cell) + 2.0f * HikariCloudRaggedBase();
    column.top += lowered;
    const float elevation = altitude - layer.base - edgeLift + lowered;
    column.height = elevation / column.top;
    column.profile = 1.0f - smoothstep(layer.falloff.x, layer.falloff.y, column.height);
    column.baseRise = layer.baseRise;
    column.baseFade = saturate(elevation / layer.baseRise);
    column.above = max(column.top - elevation, 0.0f);
    column.below = clamp(elevation, 0.0f, column.top);
    return column;
}

// この高さで雲を削る被覆率。積乱雲（雲型 0.75 以上）は上端で被覆率を広げて金床を作る。
float HikariCloudHeightCoverage(HikariCloudColumn column)
{
    const float anvil = saturate((column.type - 0.75f) * 4.0f) * smoothstep(0.7f, 0.85f, column.height);
    return pow(column.cell, lerp(1.0f, 0.5f, anvil));
}

// 雲型ごとの縁の作り方。x は侵食した濃さに掛けて 1 で切る倍率、y は細部ノイズで侵食する閾値の倍率（侵食の強さ
// 0.25 で最大 y / 4）。積雲・積乱雲（雲型 0.5 以上）は 3 と 2.4 で、縁から数十 m で雲の中の濃さ（消散 0.1/m 前後、
// 実際の積雲と同じ）に達し、縁に数十〜百数十 m の塊が残る（縁が柔らかいと半透明の中間調が広がり、日向と日陰の差が
// 出ず灰色の綿になる）。層雲（雲型 0.15 以下）は 1.3 と 0.6 で、細部の凹凸がほとんど無い柔らかな一面の雲になる
// （層雲の濃淡は大きな形の起伏だけ。細部で削ると低い角度で縞が並ぶ）。
float2 HikariCloudEdgeShaping(float type)
{
    const float billowy = smoothstep(0.15f, 0.5f, type);
    return float2(lerp(1.3f, 3.0f, billowy), lerp(0.6f, 2.4f, billowy));
}

// 細部ノイズを平均（0.5）で置き換えた侵食の閾値。
float HikariCloudMeanErosion(float2 shaping)
{
    return 0.5f * shaping.y * gCloudDensity.y;
}

// 形状ノイズ s（一様分布）× 高さ方向の濃さ profile を u として、HikariCloudDensity の濃さ
// saturate(gain × (u - start) / (1 - threshold)) を s について平均した値。start = 1 - g + threshold で立ち上がり、
// full = start + (1 - threshold) / gain で 1 に達する。threshold は細部ノイズを平均で置き換えた侵食の閾値。
float HikariCloudMeanDensity(float cell, float profile, float2 shaping)
{
    const float threshold = HikariCloudMeanErosion(shaping);
    const float start = 1.0f - cell + threshold;
    if (profile <= start)
    {
        return 0.0f;
    }
    const float full = start + (1.0f - threshold) / shaping.x;
    const float ramp = min(profile, full) - start;
    return (ramp * ramp * 0.5f / (full - start) + max(profile - full, 0.0f)) / profile;
}

// その場の雲域の被覆率と雲型（天気図の大きな揺らぎ）だけで決まる、高さ altitude の雲の濃さの平均。対流セル（1km 程度）
// も 3D ノイズも見ないので、遠方（数十 km 先）や環境光用の雲の空でセルを拾って走査ノイズにならず、雲域の濃淡で
// 地平線の雲の連なりの上端も揃わない。セルの強さ g は被覆率 c の範囲で一様なので 4 点の中点則で平均する
// （セルの外は g = 0、ただし曇天ではつながる）。
// column には被覆率と、この高さの上下の雲の平均の厚さを入れる（光の当て方に使う）。
float HikariCloudStatisticalDensity(float3 position, float altitude, out HikariCloudColumn column)
{
    column = (HikariCloudColumn)0;
    const float4 weather = HikariCloudSampleWeather(position.xz);
    const float coverage = HikariCloudCoverage(weather, position.xz);
    column.coverage = coverage;
    if (coverage <= 0.0f)
    {
        return 0.0f;
    }
    const float type = HikariCloudType(weather);
    const HikariCloudLayer layer = HikariCloudResolveLayer(type);
    const float2 shaping = HikariCloudEdgeShaping(type);
    const float scale = HikariCloudScale(layer, coverage) * HikariCloudTurret(weather);
    const float elevation = altitude - layer.base;
    float density = 0.0f;
    float meanTop = 0.0f;
    [unroll]
    for (uint index = 0u; index < 5u; ++index)
    {
        const float weight = index < 4u ? coverage * 0.25f : 1.0f - coverage;
        const float cell = HikariCloudMergeCell(index < 4u ? (float(index) + 0.5f) * 0.25f : 0.0f, coverage);
        const float top = max(scale * cell * (2.0f - cell), 1.0f);
        const float height = elevation / top;
        const float profile = height > 0.0f && height < 1.0f ? 1.0f - smoothstep(layer.falloff.x, layer.falloff.y,
            height) : 0.0f;
        density += weight * HikariCloudMeanDensity(cell, profile, shaping);
        meanTop += weight * top;
    }
    column.above = max(meanTop - elevation, 0.0f);
    column.below = clamp(elevation, 0.0f, meanTop);
    return density * saturate(elevation / layer.baseRise);
}

// 雲の濃さ（0..1）。Horizon Zero Dawn（Schneider 2015/2017）と同じ組み立て:
//   形状ノイズ（一様分布の Perlin-Worley）× 高さ方向の濃さ → 被覆率 g を閾値 1 - g として remap し g を掛ける
//   → 細部ノイズ（Worley、周期 320m）で縁を侵食する（雲底の近くは細かく波打ち、上は膨らんだ塊の縁）
//   → 雲型ごとの倍率で 1 で切る（HikariCloudEdgeShaping。積雲は縁から数十 m で中の濃さになる）。
// セルの縁（g が小さい所）はノイズの山だけが残ってちぎれる。
// detailWeight は細部ノイズを使う割合（0 なら平均で置き換える）。footprint はこの標本が代表する幅 [m]（画素の幅や
// 光源方向の歩幅）で、ノイズの mip をその幅に合わせて選び、細かすぎる凹凸を拾ってちらつかないようにする。
// 幅が形状ノイズの塊（周期の 1/16、150m）の 4 倍を超える所では、この柱の濃さの期待値 HikariCloudMeanDensity に
// 移る（ノイズの mip の平均に閾値を掛けると、セルの芯だけが残って雲が細る）。
float HikariCloudDensity(HikariCloudColumn column, float3 position, float altitude, float detailWeight, float footprint)
{
    if (column.cell <= 0.0f || column.profile <= 0.0f || column.baseFade <= 0.0f || column.height >= 1.0f)
    {
        return 0.0f;
    }
    const float coverage = HikariCloudHeightCoverage(column);
    const float2 shaping = HikariCloudEdgeShaping(column.type);
    const float meanWeight = saturate(0.5f * log2(max(footprint * 16.0f / gCloudShapePhase.z, 1.0f)));
    const float mean = meanWeight > 0.0f ? HikariCloudMeanDensity(coverage, column.profile, shaping) : 0.0f;
    // 雲底。降水する雲は波打つ（形状ノイズを見ない平均の濃さではその平均の高さ）。
    const float elevation = column.height * column.top;
    const float ragged = HikariCloudRaggedBase();
    float baseFade = saturate((elevation - ragged) / column.baseRise);
    if (meanWeight >= 1.0f)
    {
        return mean * baseFade;
    }
    const float3 shapePosition = float3(position.x + gCloudShapePhase.x, altitude + gCloudShapePhase.w,
        position.z + gCloudShapePhase.y) / gCloudShapePhase.z;
    const float shapeLod = log2(max(footprint * HIKARI_CLOUD_SHAPE_NOISE_SIZE / gCloudShapePhase.z, 1.0f));
    const float shapeNoise = gCloudShapeNoise.SampleLevel(gCloudLinearWrap, shapePosition, shapeLod);
    const float shape = shapeNoise * column.profile;
    // 雲底は形状ノイズ（数百 m の塊）で波打ち、塊の濃い所ほど低く垂れ下がる。
    float ragging = 2.0f * ragged * (1.0f - shapeNoise);
    baseFade = saturate((elevation - ragging) / column.baseRise);
    const float density = saturate(HikariCloudRemap(shape, 1.0f - coverage, 1.0f, 0.0f, 1.0f)) * coverage;
    if (density <= 0.0f)
    {
        return mean * meanWeight * baseFade;
    }
    float erosion = 0.5f;
    if (detailWeight > 0.0f)
    {
        const float swirl = (shapeNoise - 0.5f) * HIKARI_CLOUD_DETAIL_SWIRL;
        const float3 detailPosition = float3(position.x + gCloudDetailPhase.x + swirl, altitude,
            position.z + gCloudDetailPhase.y - swirl) / gCloudDetailPhase.z;
        const float detailLod = log2(max(footprint * HIKARI_CLOUD_DETAIL_NOISE_SIZE / gCloudDetailPhase.z, 1.0f));
        const float highFrequency = gCloudDetailNoise.SampleLevel(gCloudLinearWrap, detailPosition, detailLod);
        // 雲底から雲の高さの 1 割（最大 150m）は細かく波打つ縁、その上は膨らんだ塊の縁。積乱雲でも波打つ層を薄く保ち、
        // 雲底の下に半透明の幕を作らない。
        const float billow = saturate(max(column.height * 10.0f, column.height * column.top / 150.0f));
        erosion = lerp(0.5f, lerp(highFrequency, 1.0f - highFrequency, billow), detailWeight);
        // 雲底の縁は細部ノイズでも波打つ。
        ragging += HIKARI_CLOUD_DETAIL_RAGGED * (1.0f - highFrequency) * detailWeight;
        baseFade = saturate((elevation - ragging) / column.baseRise);
    }
    // 雲底は凝結高度でほぼ平らに切る。
    const float threshold = erosion * shaping.y * gCloudDensity.y;
    const float shaped = saturate(shaping.x * HikariCloudRemap(density, threshold, 1.0f, 0.0f, 1.0f));
    return lerp(shaped, mean, meanWeight) * baseFade;
}

// 消散係数 [1/m]。降水する雲は濃い。
float HikariCloudExtinction(float density)
{
    return density * gCloudDensity.x * (1.0f + 0.6f * gCloudWeather.z);
}

// 塊の凹凸による空（x）と地面（y）の光の遮り（0..1）。60m 上と下の濃さで、出っ張った塊は光を受け、くぼみは
// 受けにくい。柱の厚さだけで決める環境光では陰の側が一様になり、凹凸が見えない。
float2 HikariCloudLocalVisibility(float3 position, float detailWeight)
{
    float2 visibility = 1.0f.xx;
    [unroll]
    for (uint index = 0u; index < 2u; ++index)
    {
        const float3 probe = position + float3(0.0f, index == 0u ? 60.0f : -60.0f, 0.0f);
        const float altitude = HikariCloudAltitude(probe);
        const float density =
            HikariCloudDensity(HikariCloudResolveColumn(probe, altitude), probe, altitude, detailWeight, 60.0f);
        visibility[index] = exp(-HikariCloudExtinction(density) * 30.0f);
    }
    return visibility;
}

// 雨の濃さ（0..1）。降水する雲（被覆率の高い所）の対流セルの芯の雲底から地面まで（芯の間は雨が無く雲底が見え、
// 遠くの雨は幕になる）。細部ノイズを縦に引き伸ばして雨の筋にする。
// cloudDepth にはその上の雲の柱の鉛直の光学的厚さ（濃さの期待値 × 雲頂の高さ）を返す（雨粒の照らされ方に使う）。
float HikariCloudRainDensity(float3 position, float altitude, out float cloudDepth)
{
    cloudDepth = 0.0f;
    const float4 weather = HikariCloudSampleWeather(position.xz);
    const float type = HikariCloudType(weather);
    const HikariCloudLayer layer = HikariCloudResolveLayer(type);
    const float coverage = HikariCloudCoverage(weather, position.xz);
    const float cell = saturate((weather.z - (1.0f - coverage)) / max(coverage, 0.02f));
    const float shaft = smoothstep(0.5f, 0.9f, cell) * smoothstep(0.3f, 0.7f, coverage);
    if (shaft <= 0.0f || altitude >= layer.base)
    {
        return 0.0f;
    }
    const float merged = HikariCloudMergeCell(cell, coverage);
    const float top = HikariCloudScale(layer, coverage) * HikariCloudTurret(weather) * merged * (2.0f - merged);
    cloudDepth = HikariCloudExtinction(HikariCloudMeanDensity(merged, 0.8f, HikariCloudEdgeShaping(type))) * top;
    const float3 streakPosition = float3(position.x + gCloudDetailPhase.x, altitude * 0.25f,
        position.z + gCloudDetailPhase.y) / (gCloudDetailPhase.z * 5.0f);
    const float streak = gCloudDetailNoise.SampleLevel(gCloudLinearWrap, streakPosition, 0.0f);
    return gCloudWeather.z * shaft * lerp(0.35f, 1.0f, streak) *
        (1.0f - smoothstep(layer.base - 150.0f, layer.base, altitude));
}

float HikariCloudHenyeyGreenstein(float cosine, float g)
{
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * HIKARI_CLOUD_PI * pow(max(1.0f + g2 - 2.0f * g * cosine, 1.0e-4f), 1.5f));
}

// 前方の山（銀色の縁）と弱い後方散乱を重ねた位相関数（前方 0.7 は HDRP と同じ）。scale は異方性の倍率
// （雲の中で何度も散乱した光は向きが散って前方の山が低くなる）。
float HikariCloudPhase(float cosine, float scale)
{
    // 前方の山は見た目の倍率（gCloudLook.y、銀の縁の強さ）で増減する。
    return 0.3f * HikariCloudHenyeyGreenstein(cosine, -0.3f * scale) +
        0.7f * gCloudLook.y * HikariCloudHenyeyGreenstein(cosine, 0.7f * scale);
}

// 光源方向へ向かって、殻の中を間隔を倍にしながら光学的厚さを足す（最大 samples 点、間隔は 15m × 2^index、
// HIKARI_CLOUD_SHADOW_DISTANCE まで）。最初の HIKARI_CLOUD_LIGHT_DETAIL_SAMPLES 点（105m）は細部ノイズを見て、縁の
// 数十 m の塊どうしの影（花椰菜の凹凸の陰）を拾う。その先は柱の期待値になり、数 km 先の雲の影は、その方向の雲のセルに
// 当たるかどうかで決まる（UE5 の影の追跡距離と同じ考え方）。
static const uint HIKARI_CLOUD_LIGHT_DETAIL_SAMPLES = 3u;

float HikariCloudLightOpticalDepth(float3 position, uint samples)
{
    const float3 direction = gCloudLightDirection.xyz;
    float opticalDepth = 0.0f;
    float stepLength = 15.0f;
    float distance = 0.0f;
    [loop]
    for (uint index = 0u; index < samples && distance < HIKARI_CLOUD_SHADOW_DISTANCE; ++index)
    {
        const float3 samplePosition = position + direction * (distance + stepLength * 0.5f);
        const float altitude = HikariCloudAltitude(samplePosition);
        if (altitude > gCloudShell.y || altitude < gCloudShell.x)
        {
            return opticalDepth;
        }
        const HikariCloudColumn column = HikariCloudResolveColumn(samplePosition, altitude);
        const float density = HikariCloudDensity(column, samplePosition, altitude,
            index < HIKARI_CLOUD_LIGHT_DETAIL_SAMPLES ? 1.0f : 0.0f, stepLength);
        opticalDepth += HikariCloudExtinction(density) * stepLength;
        distance += stepLength;
        stepLength *= 2.0f;
    }
    return opticalDepth;
}

// 主光源からの散乱光 [1/sr]。多重散乱を段ごとに減衰・寄与・異方性を半分にした和で近似し（Wrenninge 2013、
// Frostbite・HDRP と同じ 3 段）、光源と反対を向いた側の表面近くを暗くする（powder、HDRP と同じ形）。
// Horizon Zero Dawn と同じく 2 倍して、powder で暗くなる分と単一散乱の和で足りない分を補う。
// 一面の雲（deck 1、曇天）は水平な板なので、段の和では届かない雲底へも拡散透過 μ / π / (1 + 0.11 τ μ) で光が届く
// （曇天の雲底が数 knits）。ちぎれた雲（deck 0）にはこの下限を付けない。付けると陰の側が主光源の色で一様に明るくなり、
// 凹凸と日向・日陰の差が消えて灰色の綿になる（陰の側は空の光と地面の照り返しで照らす）。deck は雲が横に連なっている
// 度合い、elevation は光源の仰角の sin。
float HikariCloudLightScattering(float cosine, float lightOpticalDepth, float density, float deck, float elevation)
{
    float scattering = 0.0f;
    float contribution = 1.0f;
    float attenuation = 1.0f;
    float anisotropy = 1.0f;
    [unroll]
    for (uint octave = 0u; octave < 3u; ++octave)
    {
        scattering += contribution * HikariCloudPhase(cosine, anisotropy) * exp(-attenuation * lightOpticalDepth);
        contribution *= 0.5f;
        attenuation *= 0.5f;
        anisotropy *= 0.5f;
    }
    const float powder = lerp(1.0f, saturate(2.0f * (1.0f - exp(-4.0f * density))),
        smoothstep(0.5f, -0.5f, cosine));
    const float direct = 2.0f * scattering * powder;
    const float slab = max(elevation, 0.0f) /
        (HIKARI_CLOUD_PI * (1.0f + 0.11f * lightOpticalDepth * max(elevation, 0.1f)));
    return lerp(direct, max(direct, slab), deck);
}

#endif
