cbuffer SkyCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4 gSkyZenithExposure;      // rgb zenith, a exposure
    float4 gSkyHorizonPower;        // rgb horizon, a horizon power
    float4 gSkyGroundYaw;           // rgb ground, a yaw
    float4 gSkyTintMode;            // rgb tint, a SkyMode
    float4 gSkySunDirectionIntensity;
    float4 gSkySunSizeParams;       // x sun size
};

Texture2D gSkyTex2D : register(t0);
TextureCube gSkyCube : register(t1);
SamplerState gLinearClamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 localDir : TEXCOORD0;
};

float3 RotateYaw(float3 dir, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);
    return float3(dir.x * c - dir.z * s, dir.y, dir.x * s + dir.z * c);
}

float2 DirectionToLatLongUv(float3 dir)
{
    const float invPi = 0.31830988618f;
    const float invTwoPi = 0.15915494309f;
    float u = atan2(dir.z, dir.x) * invTwoPi + 0.5f;
    float v = asin(clamp(dir.y, -1.0f, 1.0f)) * -invPi + 0.5f;
    return float2(u, v);
}

float3 SampleGradient(float3 dir)
{
    float y = saturate(dir.y * 0.5f + 0.5f);
    float3 upper = lerp(gSkyHorizonPower.rgb, gSkyZenithExposure.rgb, y);
    float3 lower = lerp(gSkyGroundYaw.rgb, gSkyHorizonPower.rgb, y);
    float3 color = (dir.y >= 0.0f) ? upper : lower;
    float horizon = pow(saturate(1.0f - abs(dir.y)), max(0.01f, gSkyHorizonPower.a));
    return lerp(color, gSkyHorizonPower.rgb, horizon * 0.25f);
}

float3 AddSunDisk(float3 color, float3 dir)
{
    float intensity = max(0.0f, gSkySunDirectionIntensity.w);
    if (intensity <= 0.0f)
    {
        return color;
    }

    float3 sunDir = normalize(-gSkySunDirectionIntensity.xyz);
    float size = max(0.0001f, gSkySunSizeParams.x);
    float sun = smoothstep(1.0f - size, 1.0f, dot(normalize(dir), sunDir));
    return color + sun * intensity;
}

float4 main(PSInput input) : SV_TARGET
{
    float3 dir = normalize(RotateYaw(input.localDir, gSkyGroundYaw.a));
    uint mode = (uint)(gSkyTintMode.a + 0.5f);

    float3 sky = SampleGradient(dir);
    if (mode == 2u)
    {
        sky = gSkyCube.Sample(gLinearClamp, dir).rgb;
    }
    else if (mode == 3u)
    {
        sky = gSkyTex2D.Sample(gLinearClamp, DirectionToLatLongUv(dir)).rgb;
    }

    sky = AddSunDisk(sky, dir);
    sky *= gSkyTintMode.rgb;
    sky *= gSkyZenithExposure.a;
    return float4(sky, 1.0f);
}
