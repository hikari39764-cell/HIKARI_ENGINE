#ifndef HIKARI_TEMPORAL_REPROJECTION
#define HIKARI_TEMPORAL_REPROJECTION

// Motion は非 jitter の current-to-previous pixel 差。履歴の raster grid へ一度だけ戻す。
float2 HikariReprojectHistoryUv(float2 uv, float2 motionPixels, float2 inverseSize,
    float2 currentJitterPixels, float2 previousJitterPixels)
{
    return uv + (motionPixels + previousJitterPixels - currentJitterPixels) * inverseSize;
}

#endif
