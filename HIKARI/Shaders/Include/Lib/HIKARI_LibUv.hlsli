#ifndef HIKARI_LIB_UV_HLSLI
#define HIKARI_LIB_UV_HLSLI

// UV の変形。角度はラジアン。

// center を軸に angle だけ回す。
float2 HikariRotateUv(float2 uv, float angle, float2 center)
{
    const float s = sin(angle), c = cos(angle);
    const float2 p = uv - center;
    return float2(c * p.x - s * p.y, s * p.x + c * p.y) + center;
}

// 極座標：x = center からの距離、y = 角度（0..1 で一周、+x から反時計回り）。
float2 HikariPolarUv(float2 uv, float2 center)
{
    const float2 p = uv - center;
    return float2(length(p), frac(atan2(p.y, p.x) / 6.28318530718f + 1.0f));
}

float2 HikariTileOffsetUv(float2 uv, float2 tiling, float2 offset) { return uv * tiling + offset; }

// columns × rows に並んだ序列の frame 番目の枠へ写す（左上から右へ、行ごとに下へ）。
float2 HikariFlipbookUv(float2 uv, float columns, float rows, float frame)
{
    const float count = max(columns * rows, 1.0f);
    const float index = fmod(floor(max(frame, 0.0f)), count);
    const float2 cell = float2(fmod(index, columns), floor(index / columns));
    return (cell + frac(uv)) / float2(columns, rows);
}

// center の周りを中心ほど強くねじる。strength は中心での回転量（ラジアン）。
float2 HikariTwirlUv(float2 uv, float2 center, float strength)
{
    const float2 p = uv - center;
    const float angle = strength * (1.0f - saturate(length(p) * 2.0f));
    return HikariRotateUv(p, angle, 0.0f.xx) + center;
}

#endif
