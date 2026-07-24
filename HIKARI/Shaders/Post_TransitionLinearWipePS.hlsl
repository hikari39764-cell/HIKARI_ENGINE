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
    float2 uv : TEXCOORD0;
};

float4 main(PS_IN i) : SV_TARGET
{
    float2 uv = i.uv;
    float4 src = gTex.Sample(gSamp, uv);

    // user[0].x = softness
    float softness = max(0.0001, user[0].x);

    // runtime packed by PostSystem
    float progress = saturate(user[14].x);
    float isTransitionIn = user[14].y;

    // left -> right wipe
    float threshold = (isTransitionIn > 0.5f) ? (1.0f - progress) : progress;
    float visible = smoothstep(threshold - softness, threshold + softness, uv.x);

    if (isTransitionIn <= 0.5f)
    {
        visible = 1.0f - visible;
    }

    return float4(src.rgb * visible, src.a);
}