#define MAX_JOINTS 128

cbuffer TemporalGeometryCB : register(b0)
{
    float4x4 gRenderViewProj;
    float4x4 gCurrentViewProj;
    float4x4 gPreviousViewProj;
    float4x4 gCurrentWorld;
    float4x4 gPreviousWorld;
    float4 gScreenParams;
    float4 gTemporalParams; // x: history valid
};

cbuffer TemporalSkinCB : register(b1)
{
    float4x4 gCurrentJoints[MAX_JOINTS];
    float4x4 gPreviousJoints[MAX_JOINTS];
};

struct StaticInput
{
    float3 position : POSITION;
};

struct SkinnedInput
{
    float3 position : POSITION;
    uint4 joints : JOINTS0;
    float4 weights : WEIGHTS0;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float4 currentClip : TEXCOORD0;
    float4 previousClip : TEXCOORD1;
};

struct PSOut
{
    float2 motionPixels : SV_TARGET0;
    float2 reprojectionMetadata : SV_TARGET1;
};

VSOut BuildOutput(float4 currentLocal, float4 previousLocal)
{
    VSOut output;
    float4 currentWorld = mul(gCurrentWorld, currentLocal);
    float4 previousWorld = mul(gPreviousWorld, previousLocal);
    output.position = mul(gRenderViewProj, currentWorld);
    output.currentClip = mul(gCurrentViewProj, currentWorld);
    output.previousClip = mul(gPreviousViewProj, previousWorld);
    return output;
}

VSOut VSStatic(StaticInput input)
{
    float4 local = float4(input.position, 1.0f);
    return BuildOutput(local, local);
}

float4 SkinPosition(float3 position, uint4 joints, float4 weights, bool previous)
{
    uint4 jointIndices = min(joints, (uint4)(MAX_JOINTS - 1));
    if (previous)
    {
        return
            mul(gPreviousJoints[jointIndices.x], float4(position, 1.0f)) * weights.x +
            mul(gPreviousJoints[jointIndices.y], float4(position, 1.0f)) * weights.y +
            mul(gPreviousJoints[jointIndices.z], float4(position, 1.0f)) * weights.z +
            mul(gPreviousJoints[jointIndices.w], float4(position, 1.0f)) * weights.w;
    }
    return
        mul(gCurrentJoints[jointIndices.x], float4(position, 1.0f)) * weights.x +
        mul(gCurrentJoints[jointIndices.y], float4(position, 1.0f)) * weights.y +
        mul(gCurrentJoints[jointIndices.z], float4(position, 1.0f)) * weights.z +
        mul(gCurrentJoints[jointIndices.w], float4(position, 1.0f)) * weights.w;
}

VSOut VSSkinned(SkinnedInput input)
{
    return BuildOutput(
        SkinPosition(input.position, input.joints, input.weights, false),
        SkinPosition(input.position, input.joints, input.weights, true));
}

PSOut PSMain(VSOut input)
{
    float2 currentNdc = input.currentClip.xy / max(abs(input.currentClip.w), 1e-5f);
    float2 previousNdc = input.previousClip.xy / max(abs(input.previousClip.w), 1e-5f);
    float2 currentUv = float2(currentNdc.x * 0.5f + 0.5f, -currentNdc.y * 0.5f + 0.5f);
    float2 previousUv = float2(previousNdc.x * 0.5f + 0.5f, -previousNdc.y * 0.5f + 0.5f);
    float previousDepth =
        input.previousClip.z / max(abs(input.previousClip.w), 1e-5f);
    PSOut output;
    // Keep object motion in the same current-to-previous pixel-space contract
    // as the dense camera-motion pass.
    output.motionPixels = (previousUv - currentUv) * gScreenParams.xy;
    output.reprojectionMetadata = float2(
        saturate(previousDepth),
        gTemporalParams.x >= 0.5f ? 1.0f : 0.0f);
    return output;
}
