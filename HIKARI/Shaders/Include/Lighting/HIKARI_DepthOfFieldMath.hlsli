#ifndef HIKARI_DEPTH_OF_FIELD_MATH_INCLUDED
#define HIKARI_DEPTH_OF_FIELD_MATH_INCLUDED

float2 HikariDepthOfFieldCoc(float viewDepth, float4 focusBand, float2 blurRadii)
{
    const float halfFocusRange = max(0.0f, focusBand.y * 0.5f);
    const float nearStart = focusBand.x - halfFocusRange;
    const float farStart = focusBand.x + halfFocusRange;
    const float nearCoc = saturate((nearStart - viewDepth) / max(focusBand.z, 1.0e-4f)) * blurRadii.x;
    const float farCoc = saturate((viewDepth - farStart) / max(focusBand.w, 1.0e-4f)) * blurRadii.y;
    return float2(nearCoc, farCoc);
}

#endif
