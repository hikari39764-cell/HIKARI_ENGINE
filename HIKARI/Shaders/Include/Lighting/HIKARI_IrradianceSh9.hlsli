#ifndef HIKARI_IRRADIANCE_SH9_INCLUDED
#define HIKARI_IRRADIANCE_SH9_INCLUDED

// .hlpv は入射放射輝度の SH9 係数を保持する。戻り値は Lambert 畳み込み E/pi の重み。
// Sky IBL / Surface Lightmap と同じ単位で、呼び出し側は反照率を一度だけ掛ける。
float HikariSh9DiffuseWeight(float3 normal, uint coefficient)
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
    const float convolution[9] = {
        1.0f,
        2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f,
        0.25f, 0.25f, 0.25f, 0.25f, 0.25f
    };
    return basis[coefficient] * convolution[coefficient];
}

#endif
