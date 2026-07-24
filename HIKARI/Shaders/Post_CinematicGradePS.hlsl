Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

cbuffer CommonParams : register(b0)
{
    float time;
    float deltaTime;
    float combo;
    float intensity;

    float resolutionX;
    float resolutionY;
    float pad0;
    float pad1;

    float4 user[16];
};

struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float3 ApplySaturation(float3 c, float saturation)
{
    float luma = dot(c, float3(0.299, 0.587, 0.114));
    return lerp(luma.xxx, c, saturation);
}

float3 ApplyContrast(float3 c, float contrast)
{
    return (c - 0.5) * contrast + 0.5;
}

float4 main(PS_IN i) : SV_TARGET
{
    float2 uv = i.uv;

    float vignette     = user[0].x;
    float chroma       = user[0].y;
    float saturation   = user[0].z;
    float contrast     = user[0].w;

    float gain         = user[1].x;
    float grainAmount  = user[1].y;
    float scanlineAmp  = user[1].z;
    float scanlineFreq = user[1].w;

    float2 ca = float2(chroma, 0.0);
    float r = gTex.Sample(gSamp, uv + ca).r;
    float g = gTex.Sample(gSamp, uv).g;
    float b = gTex.Sample(gSamp, uv - ca).b;

    float3 color = float3(r, g, b);

    color = ApplySaturation(color, saturation);
    color = ApplyContrast(color, contrast);
    color *= gain;

    float2 center = uv * 2.0 - 1.0;
    float vig = 1.0 - dot(center, center) * vignette;
    color *= saturate(vig);

    float scanline = 1.0 + sin(uv.y * scanlineFreq + time * 1.5) * scanlineAmp;
    color *= scanline;

    float grain = Hash12(uv * float2(resolutionX, resolutionY) + time * 31.7) - 0.5;
    color += grain * grainAmount;

    return float4(saturate(color), 1.0);
}
