cbuffer AfterimageCameraConstants : register(b0)
{
    float4x4 gViewProj;
    float4 gCameraPos;
};

cbuffer AfterimageDrawConstants : register(b1)
{
    float4x4 gWorld;
    // x: base opacity, y: intensity, z: local effect scale,
    // w: shell offset in world units.
    float4 gEffect0;
    // x: age seconds, y: lifetime seconds, z: Fresnel power,
    // w: dissolve strength.
    float4 gEffect1;
    float4 gColor;
    float4x4 gJointMatrices[128];
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 joints : JOINTS0;
    float4 weights : WEIGHTS0;
};

struct VSStaticInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD1;
};

float4x4 ResolveJoint(uint index)
{
    return gJointMatrices[min(index, 127u)];
}

VSOutput BuildAfterimageVertex(
    float3 localPosition,
    float3 localNormal,
    float2 uv)
{
    VSOutput output;

    localPosition *= gEffect0.z;
    float4 worldPosition = mul(gWorld, float4(localPosition, 1.0f));
    float3 transformedNormal = mul((float3x3)gWorld, localNormal);
    float normalLengthSq = dot(transformedNormal, transformedNormal);
    float3 worldNormal = normalLengthSq > 1.0e-5f
        ? transformedNormal * rsqrt(normalLengthSq)
        : float3(0.0f, 1.0f, 0.0f);
    worldPosition.xyz += worldNormal * gEffect0.w;

    output.position = mul(gViewProj, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = worldNormal;
    output.uv = uv;
    return output;
}

VSOutput VSMain(VSInput input)
{

    float4 skinnedPosition =
        mul(ResolveJoint(input.joints.x), float4(input.position, 1.0f)) * input.weights.x +
        mul(ResolveJoint(input.joints.y), float4(input.position, 1.0f)) * input.weights.y +
        mul(ResolveJoint(input.joints.z), float4(input.position, 1.0f)) * input.weights.z +
        mul(ResolveJoint(input.joints.w), float4(input.position, 1.0f)) * input.weights.w;
    float3 skinnedNormal =
        mul((float3x3)ResolveJoint(input.joints.x), input.normal) * input.weights.x +
        mul((float3x3)ResolveJoint(input.joints.y), input.normal) * input.weights.y +
        mul((float3x3)ResolveJoint(input.joints.z), input.normal) * input.weights.z +
        mul((float3x3)ResolveJoint(input.joints.w), input.normal) * input.weights.w;

    float weightSum = input.weights.x + input.weights.y +
        input.weights.z + input.weights.w;
    if (weightSum <= 1.0e-5f)
    {
        skinnedPosition = float4(input.position, 1.0f);
        skinnedNormal = input.normal;
    }

    return BuildAfterimageVertex(
        skinnedPosition.xyz,
        skinnedNormal,
        input.uv);
}

VSOutput VSStaticMain(VSStaticInput input)
{
    return BuildAfterimageVertex(
        input.position,
        input.normal,
        input.uv);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 viewDelta = gCameraPos.xyz - input.worldPosition;
    float viewLengthSq = dot(viewDelta, viewDelta);
    float3 viewDirection = viewLengthSq > 1.0e-5f
        ? viewDelta * rsqrt(viewLengthSq)
        : float3(0.0f, 0.0f, 1.0f);
    float normalLengthSq = dot(input.worldNormal, input.worldNormal);
    float3 surfaceNormal = normalLengthSq > 1.0e-5f
        ? input.worldNormal * rsqrt(normalLengthSq)
        : float3(0.0f, 1.0f, 0.0f);
    float facing = saturate(dot(surfaceNormal, viewDirection));
    float fresnel = pow(
        1.0f - facing,
        max(gEffect1.z, 0.25f));

    // A deterministic, texture-free dissolve keeps this private pass
    // independent of material residency while retaining a watery shimmer.
    float noise = frac(
        sin(dot(input.worldPosition, float3(12.9898f, 78.233f, 37.719f)) +
            dot(input.uv, float2(17.13f, 9.71f))) * 43758.5453f);
    float lifetime = max(gEffect1.y, 0.001f);
    float normalizedAge = saturate(gEffect1.x / lifetime);
    float dissolveThreshold = saturate(
        normalizedAge * max(gEffect1.w, 0.1f));
    float dissolve = smoothstep(
        dissolveThreshold - 0.22f,
        dissolveThreshold + 0.22f,
        noise);
    float remaining = saturate(1.0f - normalizedAge);
    float shimmer = 0.75f + 0.25f * sin(
        dot(input.worldPosition, float3(2.1f, 3.7f, 1.4f)) +
        input.uv.x * 6.28318f);
    const float fresnelCoverage = gColor.a > 0.5f
        ? (0.22f + 0.78f * fresnel)
        : fresnel;
    float alpha = saturate(
        gEffect0.x * gEffect0.y * remaining * remaining * dissolve *
        fresnelCoverage);
    float3 color = gColor.rgb * shimmer *
        (0.72f + 0.85f * fresnel) * gEffect0.y;
    return float4(color * alpha, alpha);
}
