// 侵蝕の GPU 版。式は HIKARI_TerrainDataErosion.cpp（CPU の参照実装）と同じで、順番も同じ。
// 水力：Mei, Decaudin, Hu 2007 の仮想パイプ。熱力：安息角を超えた高低差を 8 近傍へ配る（Musgrave 1989）。
// どの pass も前の状態だけを読んで次の状態へ書く（Jacobi 型）。

cbuffer ErosionConstants : register(b0)
{
    uint gWidth;
    uint gHeight;
    float gSpacing;
    float gTimeStep;
    float gRainStep;
    float gCapacity;
    float gDissolve;
    float gDeposit;
    float gKeepWater;
    float gMinimumTilt;
    float gMaximumErosionDepth;
    float gThermalRate;
    float gTalusAxis;
    float gTalusDiagonal;
    float gReserved0;
    float gReserved1;
};

struct Cell
{
    float ground;
    float water;
    float sediment;
    float eroded;
};

StructuredBuffer<Cell> gIn : register(t0);
RWStructuredBuffer<Cell> gOut : register(u0);
// 水力：L R T B の流出量。熱力：8 方向の前半。
RWStructuredBuffer<float4> gLanesA : register(u1);
// 水力：流速 xz。熱力：8 方向の後半。
RWStructuredBuffer<float4> gLanesB : register(u2);
// x 流量の積分、y 堆積の総量、z 磨耗の総量。
RWStructuredBuffer<float4> gMaps : register(u3);

static const float kGravity = 9.81f;
static const float kMinimumWaterDepth = 1.0e-5f;

uint IndexOf(uint x, uint z) { return z * gWidth + x; }

#define BEGIN_CELL \
    if (id.x >= gWidth || id.y >= gHeight) return; \
    const uint x = id.x; \
    const uint z = id.y; \
    const uint i = IndexOf(x, z);

// 降雨と 4 方向の流出量。
[numthreads(32, 32, 1)]
void CSFlux(uint3 id : SV_DispatchThreadID)
{
    BEGIN_CELL
    Cell cell = gIn[i];
    cell.water += gRainStep;
    const float surface = cell.ground + cell.water;
    const float area = gSpacing * gSpacing;
    const float pipe = gTimeStep * area * kGravity / gSpacing;
    float4 flux = gLanesA[i];
    // 雨は全ての標本に同じだけ降るので、水位差には効かない。
    const float rain = gRainStep;
    flux.x = x > 0u ? max(0.0f, flux.x + pipe * (surface - gIn[i - 1u].ground - gIn[i - 1u].water - rain)) : 0.0f;
    flux.y = x + 1u < gWidth ? max(0.0f, flux.y + pipe * (surface - gIn[i + 1u].ground - gIn[i + 1u].water - rain)) : 0.0f;
    flux.z = z > 0u ? max(0.0f, flux.z + pipe * (surface - gIn[i - gWidth].ground - gIn[i - gWidth].water - rain)) : 0.0f;
    flux.w = z + 1u < gHeight ? max(0.0f, flux.w + pipe * (surface - gIn[i + gWidth].ground - gIn[i + gWidth].water - rain)) : 0.0f;
    const float total = (flux.x + flux.y + flux.z + flux.w) * gTimeStep;
    if (total > 0.0f)
        flux *= min(1.0f, cell.water * area / total);
    gLanesA[i] = flux;
    gOut[i] = cell;
}

// 水深と流速。
[numthreads(32, 32, 1)]
void CSWater(uint3 id : SV_DispatchThreadID)
{
    BEGIN_CELL
    Cell cell = gIn[i];
    const float4 flux = gLanesA[i];
    const float inLeft = x > 0u ? gLanesA[i - 1u].y : 0.0f;
    const float inRight = x + 1u < gWidth ? gLanesA[i + 1u].x : 0.0f;
    const float inTop = z > 0u ? gLanesA[i - gWidth].w : 0.0f;
    const float inBottom = z + 1u < gHeight ? gLanesA[i + gWidth].z : 0.0f;
    const float outgoing = flux.x + flux.y + flux.z + flux.w;
    const float next = max(0.0f, cell.water + gTimeStep * (inLeft + inRight + inTop + inBottom - outgoing) / (gSpacing * gSpacing));
    const float depth = 0.5f * (cell.water + next);
    const float passX = 0.5f * (inLeft - flux.x + flux.y - inRight);
    const float passZ = 0.5f * (inTop - flux.z + flux.w - inBottom);
    const bool wet = depth > kMinimumWaterDepth;
    gLanesB[i] = float4(wet ? passX / (depth * gSpacing) : 0.0f, wet ? passZ / (depth * gSpacing) : 0.0f, 0.0f, 0.0f);
    float4 maps = gMaps[i];
    maps.x += sqrt(passX * passX + passZ * passZ) * gTimeStep;
    gMaps[i] = maps;
    cell.water = next;
    gOut[i] = cell;
}

// 運搬能力と浸食・堆積。
[numthreads(32, 32, 1)]
void CSErode(uint3 id : SV_DispatchThreadID)
{
    BEGIN_CELL
    Cell cell = gIn[i];
    const float2 velocity = gLanesB[i].xy;
    const float gx = (gIn[IndexOf(min(x + 1u, gWidth - 1u), z)].ground - gIn[IndexOf(x > 0u ? x - 1u : x, z)].ground) / (2.0f * gSpacing);
    const float gz = (gIn[IndexOf(x, min(z + 1u, gHeight - 1u))].ground - gIn[IndexOf(x, z > 0u ? z - 1u : z)].ground) / (2.0f * gSpacing);
    const float slope2 = gx * gx + gz * gz;
    const float tilt = max(sqrt(slope2 / (1.0f + slope2)), gMinimumTilt);
    const float capacity = gCapacity * tilt * length(velocity);
    float4 maps = gMaps[i];
    if (capacity > cell.sediment)
    {
        const float budget = max(0.0f, gMaximumErosionDepth - cell.eroded);
        const float amount = min(gDissolve * (capacity - cell.sediment) * gTimeStep, budget);
        cell.ground -= amount;
        cell.sediment += amount;
        cell.eroded += amount;
        maps.z += amount;
    }
    else
    {
        const float amount = min(gDeposit * (cell.sediment - capacity) * gTimeStep, cell.sediment);
        cell.ground += amount;
        cell.sediment -= amount;
        maps.y += amount;
    }
    gMaps[i] = maps;
    gOut[i] = cell;
}

// 堆積物を流速で半ラグランジュ移流し、水を蒸発させる。
[numthreads(32, 32, 1)]
void CSTransport(uint3 id : SV_DispatchThreadID)
{
    BEGIN_CELL
    Cell cell = gIn[i];
    const float2 velocity = gLanesB[i].xy;
    const float sx = clamp(float(x) - velocity.x * gTimeStep / gSpacing, 0.0f, float(gWidth - 1u));
    const float sz = clamp(float(z) - velocity.y * gTimeStep / gSpacing, 0.0f, float(gHeight - 1u));
    const uint x0 = min(uint(sx), gWidth - 2u);
    const uint z0 = min(uint(sz), gHeight - 2u);
    const float fx = sx - float(x0);
    const float fz = sz - float(z0);
    const float top = lerp(gIn[IndexOf(x0, z0)].sediment, gIn[IndexOf(x0 + 1u, z0)].sediment, fx);
    const float bottom = lerp(gIn[IndexOf(x0, z0 + 1u)].sediment, gIn[IndexOf(x0 + 1u, z0 + 1u)].sediment, fx);
    cell.sediment = lerp(top, bottom, fz);
    cell.water *= gKeepWater;
    gOut[i] = cell;
}

// 最後に運ばれている堆積物はその場に落とす（質量を消さない）。
[numthreads(32, 32, 1)]
void CSSettle(uint3 id : SV_DispatchThreadID)
{
    BEGIN_CELL
    Cell cell = gIn[i];
    float4 maps = gMaps[i];
    maps.y += cell.sediment;
    gMaps[i] = maps;
    cell.ground += cell.sediment;
    cell.sediment = 0.0f;
    gOut[i] = cell;
}

// 水力の前に、熱力が使った流路を空にする。
[numthreads(32, 32, 1)]
void CSClearLanes(uint3 id : SV_DispatchThreadID)
{
    BEGIN_CELL
    gLanesA[i] = 0.0f.xxxx;
    gLanesB[i] = 0.0f.xxxx;
    gOut[i] = gIn[i];
}

static const int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// 各標本が 8 近傍へ出す量（安息角を超えた分の半分 × rate を超過の比で配る）。
[numthreads(32, 32, 1)]
void CSThermalFlow(uint3 id : SV_DispatchThreadID)
{
    BEGIN_CELL
    const float height = gIn[i].ground;
    float excess[8];
    float total = 0.0f;
    float largest = 0.0f;
    for (uint n = 0u; n < 8u; ++n)
    {
        excess[n] = 0.0f;
        const int nx = int(x) + kDx[n];
        const int nz = int(z) + kDz[n];
        if (nx < 0 || nz < 0 || nx >= int(gWidth) || nz >= int(gHeight))
            continue;
        const float threshold = (kDx[n] != 0 && kDz[n] != 0) ? gTalusDiagonal : gTalusAxis;
        excess[n] = max(0.0f, height - gIn[IndexOf(uint(nx), uint(nz))].ground - threshold);
        total += excess[n];
        largest = max(largest, excess[n]);
    }
    const float scale = total > 0.0f ? gThermalRate * 0.5f * largest / total : 0.0f;
    gLanesA[i] = float4(excess[0], excess[1], excess[2], excess[3]) * scale;
    gLanesB[i] = float4(excess[4], excess[5], excess[6], excess[7]) * scale;
    gOut[i] = gIn[i];
}

float ThermalLane(uint index, uint n)
{
    return n < 4u ? gLanesA[index][n] : gLanesB[index][n - 4u];
}

// 出した分を引き、近傍から来た分を足す（総量は保存）。
[numthreads(32, 32, 1)]
void CSThermalApply(uint3 id : SV_DispatchThreadID)
{
    BEGIN_CELL
    Cell cell = gIn[i];
    float outgoing = 0.0f;
    float incoming = 0.0f;
    for (uint n = 0u; n < 8u; ++n)
    {
        outgoing += ThermalLane(i, n);
        const int nx = int(x) + kDx[n];
        const int nz = int(z) + kDz[n];
        if (nx >= 0 && nz >= 0 && nx < int(gWidth) && nz < int(gHeight))
            incoming += ThermalLane(IndexOf(uint(nx), uint(nz)), 7u - n);
    }
    cell.ground += incoming - outgoing;
    float4 maps = gMaps[i];
    maps.z += outgoing;
    maps.y += incoming;
    gMaps[i] = maps;
    gOut[i] = cell;
}
