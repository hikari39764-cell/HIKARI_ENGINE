#ifndef HIKARI_LIB_DITHER_HLSLI
#define HIKARI_LIB_DITHER_HLSLI

// 抖動の閾値。IGN（時間方向にも散らす）は Include/Post/HIKARI_PostHash.hlsli の HikariInterleavedGradientNoise。

// 4x4 の Bayer 行列（-0.5..0.5）。
float HikariBayer4x4(uint2 pixel)
{
    static const float kBayer[16] = {0.0f, 8.0f, 2.0f, 10.0f, 12.0f, 4.0f, 14.0f, 6.0f,
                                     3.0f, 11.0f, 1.0f, 9.0f, 15.0f, 7.0f, 13.0f, 5.0f};
    return (kBayer[(pixel.y & 3u) * 4u + (pixel.x & 3u)] + 0.5f) / 16.0f - 0.5f;
}

#endif
