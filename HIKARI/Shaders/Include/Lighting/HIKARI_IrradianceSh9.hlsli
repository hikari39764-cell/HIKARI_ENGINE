#ifndef HIKARI_IRRADIANCE_SH9_INCLUDED
#define HIKARI_IRRADIANCE_SH9_INCLUDED

struct HikariIrradianceVolumeData
{
    float4 originSampling;
    float4 spacingIntensity;
    float4 countsProbeCount;
    float4 sizeBlendDistance;
    float4 selectionParams;
};

// 動的 GI の volume（手置きの区域、近景、遠景）。C++ の LIGHTPROBE::kDynamicGiVolumeCount と同じ数。
#define HIKARI_DYNAMIC_GI_VOLUME_COUNT 3

// .hlpv は入射放射輝度の SH9 係数を保持する。戻り値は Lambert 畳み込み E/pi の重み。
// Sky IBL / Surface Lightmap と同じ単位で、呼び出し側は反照率を一度だけ掛ける。
float HikariSh9Basis(float3 normal, uint coefficient)
{
    const float x = normal.x;
    const float y = normal.y;
    const float z = normal.z;
    const float basis[9] = {
        0.282095f,
        0.488603f * y,
        0.488603f * z,
        0.488603f * x,
        1.092548f * x * y,
        1.092548f * y * z,
        0.315392f * (3.0f * z * z - 1.0f),
        1.092548f * x * z,
        0.546274f * (x * x - y * y)
    };
    return basis[coefficient];
}

// Lambert の畳み込み（帯ごとに 1、2/3、1/4）に窓 1、0.75、0.4 を掛ける。そのままだと一方向の強い光（日の当たった
// 床など）に対して約 120 度の方向が負になり、切り捨てた所が probe の並びに沿って暗い斑になる。窓を掛けると一方向の
// 光の復元が (1+cos)^2 の形になり、どんな光の分布でも負にならない。平均（0 次）は変えない。
float HikariSh9DiffuseWeight(float3 normal, uint coefficient)
{
    const float convolution[9] = {
        1.0f,
        0.5f, 0.5f, 0.5f,
        0.1f, 0.1f, 0.1f, 0.1f, 0.1f
    };
    return HikariSh9Basis(normal, coefficient) * convolution[coefficient];
}

#endif
