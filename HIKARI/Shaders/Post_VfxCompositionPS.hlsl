#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
Texture2D<float4> gLayer : register(t0);
SamplerState gSampler : register(s0);

float4 Read(float2 uv) {
    if (any(uv < 0) || any(uv >= 1)) return 0;
    float4 value = gLayer.SampleLevel(gSampler, uv, 0);
    value.rgb = clamp(value.rgb, 0, 65504);
    return value;
}
float Coverage(float4 value) {
    // 加算発光は alpha=0。水墨への変換時だけ発光の輪郭から被覆率を得る。
    return max(value.a, saturate(max(value.r, max(value.g, value.b))));
}
float3 Hue(float3 rgb, float degrees) {
    float s, c; sincos(degrees * 0.01745329252, s, c);
    float d = (1 + 2*c)/3;
    float a = (1-c)/3 - s*0.57735026919;
    float b = (1-c)/3 + s*0.57735026919;
    return max(0, float3(dot(rgb,float3(d,a,b)), dot(rgb,float3(b,d,a)), dot(rgb,float3(a,b,d))));
}
float4 Composite(HikariPostEffectInput input) {
    float2 dimensions = float2(gResolutionX, gResolutionY);
    float2 texel = rcp(dimensions);
    float4 source = Read(input.uv);
    float4 p = gUser[1];
    uint kind = (uint)round(gUser[0].x);
    // MaterialEffectKind: shared post ABI, C++ assertions guard these IDs.
    if (kind == 17) { // Pixelate: pixel size, mix
        float cell = clamp(round(p.x), 1, 256);
        int2 coordinate = int2(clamp((floor(input.uv*dimensions/cell)+0.5)*cell, 0, dimensions-1));
        return lerp(source, gLayer.Load(int3(coordinate,0)), saturate(p.y));
    }
    if (kind == 8) return source * saturate(p.x); // Opacity
    float association = source.a > 0.00001 ? source.a : 1;
    float3 color = source.rgb / association;
    if (kind == 15) { // ColorCorrect, linear HDR
        if (source.a <= 0.00001 && all(source.rgb == 0)) return 0;
        color = ((color-p.w)*p.x-0.5)*p.y+0.5;
        color = lerp(dot(color,float3(0.299,0.587,0.114)).xxx,color,p.z);
        return float4(Hue(color, gUser[2].x)*association, source.a);
    }
    if (kind == 18) { // Posterize: bounded HDR compression, then restore radiance
        float levels = clamp(round(p.x), 2, 64);
        float3 bounded = max(0,color)/(1+max(0,color));
        float3 quantized = floor(bounded*levels)/levels;
        float3 result = quantized / max(0.0001,1-quantized);
        return float4(lerp(source.rgb,result*association,saturate(p.y)),source.a);
    }
    if (kind == 10 || kind == 9) { // Separable Gaussian blur; glow extracts before H and tints after V
        float radius = clamp(kind == 10 ? p.x : p.z, 0, 64);
        bool horizontal = gUser[0].y < 1.5;
        float2 axis = horizontal ? float2(texel.x,0) : float2(0,texel.y);
        float4 blurred = 0;
        float weights = 0;
        [unroll] for (int tap=-16; tap<=16; ++tap) {
            float distance = tap / 16.0;
            float weight = exp(-4.5 * distance * distance);
            float4 sample = Read(input.uv + axis*distance*radius);
            if (kind == 9 && horizontal) {
                float luminance = dot(sample.rgb,float3(0.2126,0.7152,0.0722));
                sample.rgb *= max(0,luminance-max(0,p.x))/max(luminance,0.0001);
                sample.a = 0;
            }
            blurred += sample * weight;
            weights += weight;
        }
        blurred /= weights;
        if (kind == 9 && !horizontal) blurred.rgb *= max(0,p.y)*max(0,gUser[3].rgb);
        return blurred;
    }
    if (kind == 19) { // Ink wash: silhouette, pigment variation, edge pooling; no opaque paper background
        float2 offset = texel*clamp(p.x,0.25,12);
        float left = Coverage(Read(input.uv-float2(offset.x,0)));
        float right = Coverage(Read(input.uv+float2(offset.x,0)));
        float top = Coverage(Read(input.uv-float2(0,offset.y)));
        float bottom = Coverage(Read(input.uv+float2(0,offset.y)));
        float coverage = Coverage(source);
        float edge = saturate(abs(right-left)+abs(bottom-top));
        float2 grainUv = floor(input.uv*dimensions*0.5);
        float grain = frac(sin(dot(grainUv,float2(12.9898,78.233)))*43758.5453);
        float luminance = dot(max(color,0),float3(0.299,0.587,0.114));
        float wash = luminance/(1+luminance);
        float pigment = saturate(0.3 + (1-wash)*0.5 + edge*max(0,p.y) + (grain-0.5)*saturate(p.z));
        float3 ink = lerp(gUser[3].rgb + 0.22, gUser[3].rgb, pigment);
        coverage *= saturate(1-(1-coverage)*grain*saturate(p.z)) * saturate(gUser[3].a);
        return lerp(source, float4(max(0,ink)*coverage, coverage), saturate(p.w));
    }
    return source;
}

float4 main(HikariPostEffectInput input) : SV_TARGET {
    float4 value = Composite(input);
    // 合成先は RGBA16F。露出を重ねても次段へ infinity/NaN を渡さない。
    value.rgb = clamp(value.rgb, 0, 65504);
    value.r = isnan(value.r) ? 0 : value.r;
    value.g = isnan(value.g) ? 0 : value.g;
    value.b = isnan(value.b) ? 0 : value.b;
    value.a = saturate(value.a);
    return value;
}
