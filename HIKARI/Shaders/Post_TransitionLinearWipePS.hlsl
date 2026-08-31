#include "Include/Post/HIKARI_PostEffectInterface.hlsli"

Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

float4 main(HikariPostEffectInput input) : SV_TARGET
{
    float2 uv = input.uv;
    float4 src = gTex.Sample(gSamp, uv);

    // gUser[0].x = softness
    float softness = max(0.0001, gUser[0].x);

    // runtime packed by PostSystem
    float progress = saturate(gUser[14].x);
    float isTransitionIn = gUser[14].y;

    // left -> right wipe
    float threshold = (isTransitionIn > 0.5f) ? (1.0f - progress) : progress;
    float visible = smoothstep(threshold - softness, threshold + softness, uv.x);

    if (isTransitionIn <= 0.5f)
    {
        visible = 1.0f - visible;
    }

    return float4(src.rgb * visible, src.a);
}
