cbuffer TemporalMotionVectorCB : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gViewProj;
    float4x4 gPrevViewProj;
    float4 gScreenParams;  // xy: render size, zw: inverse render size
    float4 gHistoryParams; // x: history valid, y: depth valid, z: far plane
    float4 gCameraPos;
    float4 gPrevCameraPos;
};

Texture2D<float> gSceneDepth : register(t0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PSOut
{
    float2 motionPixels : SV_TARGET0;
    float2 reprojectionMetadata : SV_TARGET1;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = pos;
    output.position =
        float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float3 ReconstructWorld(float2 uv, float depth)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 world = mul(gInvViewProj, float4(ndc, depth, 1.0f));
    return world.xyz / max(abs(world.w), 1e-5f);
}

float3 ProjectUvDepth(float4x4 viewProj, float3 worldPos)
{
    float4 clip = mul(viewProj, float4(worldPos, 1.0f));
    float3 ndc = clip.xyz / max(abs(clip.w), 1e-5f);
    return float3(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f, ndc.z);
}

PSOut PSMain(VSOut input)
{
    if (gHistoryParams.y < 0.5f)
    {
        PSOut invalidOutput;
        invalidOutput.motionPixels = 0.0f;
        invalidOutput.reprojectionMetadata = float2(1.0f, 0.0f);
        return invalidOutput;
    }

    uint2 pixel = min(
        uint2(input.position.xy),
        max(uint2(gScreenParams.xy), uint2(1u, 1u)) - 1u);
    float depth = gSceneDepth.Load(int3(pixel, 0)).r;
    float3 currentWorld = ReconstructWorld(input.uv, depth);
    float3 previousWorld = currentWorld;
    if (depth >= 0.99999f)
    {
        float3 worldDirection = normalize(currentWorld - gCameraPos.xyz);
        currentWorld = gCameraPos.xyz + worldDirection * gHistoryParams.z;
        previousWorld = gPrevCameraPos.xyz + worldDirection * gHistoryParams.z;
    }

    float3 currentProjection = ProjectUvDepth(gViewProj, currentWorld);
    float3 previousProjection = ProjectUvDepth(gPrevViewProj, previousWorld);
    // Canonical temporal contract: previousUv = currentUv + motionPixels / size.
    // Jitter is excluded; the vector points from the current sample to its
    // previous-frame location, matching temporal upscaler conventions.
    float2 motionPixels =
        (previousProjection.xy - currentProjection.xy) * gScreenParams.xy;
    float valid = gHistoryParams.x >= 0.5f ? 1.0f : 0.0f;
    PSOut output;
    output.motionPixels = motionPixels;
    output.reprojectionMetadata = float2(saturate(previousProjection.z), valid);
    return output;
}
