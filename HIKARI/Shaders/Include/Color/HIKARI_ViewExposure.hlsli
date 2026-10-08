#ifndef HIKARI_VIEW_EXPOSURE_HLSLI
#define HIKARI_VIEW_EXPOSURE_HLSLI

// ビューの露出係数 float4 {P, 1/P, P/E, P/P_prev}（C++ は Render3D/Color/HIKARI_ViewExposure.h）。
// HDR 目標には物理量 × P を書く。表示参照の色（1.0 = 露出後の白）は × P/E で書き、露出に依らず同じ明るさに見える。

float3 HikariPreExpose(float3 radiance, float4 viewExposure)
{
    return radiance * viewExposure.x;
}

float3 HikariDisplayReferredToScene(float3 color, float4 viewExposure)
{
    return color * viewExposure.z;
}

// 表示参照の色を、露出 E の下で同じ明るさに見える絶対量（color / E）へ換算する。
// 霧などの絶対量の処理を通した後で HikariPreExpose する経路で使う。
float3 HikariDisplayReferredToRadiance(float3 color, float4 viewExposure)
{
    return color * (viewExposure.z * viewExposure.y);
}

// HDR 目標から読んだ値を絶対量へ戻す。
float3 HikariRemovePreExposure(float3 value, float4 viewExposure)
{
    return value * viewExposure.y;
}

// 絶対量を表示参照の値（× E）へ写す。表示上の明るさで頭を抑える処理に使う。
float3 HikariRadianceToDisplayReferred(float3 radiance, float4 viewExposure)
{
    return radiance * (viewExposure.x / max(viewExposure.z, 1e-20f));
}

// 前露出した HDR 目標（FP16）に収まる絶対量の上限。
float HikariMaxSceneRadiance(float4 viewExposure)
{
    return 65504.0f * viewExposure.y;
}

#endif
