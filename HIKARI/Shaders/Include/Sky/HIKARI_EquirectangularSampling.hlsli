#ifndef HIKARI_EQUIRECTANGULAR_SAMPLING_HLSLI
#define HIKARI_EQUIRECTANGULAR_SAMPLING_HLSLI

float2 HikariDirectionToEquirectangularUv(float3 direction)
{
    const float inversePi = 0.31830988618f;
    const float inverseTwoPi = 0.15915494309f;
    return float2(
        atan2(direction.z, direction.x) * inverseTwoPi + 0.5f,
        asin(clamp(direction.y, -1.0f, 1.0f)) * -inversePi + 0.5f);
}

float HikariWrapEquirectangularDerivative(float derivative)
{
    // atan2/frac jump by one at the longitude seam.  Implicit gradients see
    // that jump as a full-texture footprint and select a coarse mip, which
    // turns an otherwise seamless panorama into a blurred vertical stripe.
    return derivative - round(derivative);
}

void HikariResolveEquirectangularGradients(
    float3 direction,
    float2 uv,
    out float2 uvDerivativeX,
    out float2 uvDerivativeY)
{
    uvDerivativeX = ddx(uv);
    uvDerivativeY = ddy(uv);
    uvDerivativeX.x = HikariWrapEquirectangularDerivative(
        uvDerivativeX.x);
    uvDerivativeY.x = HikariWrapEquirectangularDerivative(
        uvDerivativeY.x);

    // Longitude is undefined at either pole.  Scaling its footprint by the
    // latitude-circle radius keeps the physical angular footprint finite and
    // prevents a tiny polar cap from selecting a panorama-wide coarse mip.
    const float latitudeCircleRadius = sqrt(saturate(
        1.0f - direction.y * direction.y));
    uvDerivativeX.x *= latitudeCircleRadius;
    uvDerivativeY.x *= latitudeCircleRadius;

    if (!all(isfinite(uvDerivativeX)) ||
        !all(isfinite(uvDerivativeY)))
    {
        uvDerivativeX = 0.0f.xx;
        uvDerivativeY = 0.0f.xx;
    }
}

#endif // HIKARI_EQUIRECTANGULAR_SAMPLING_HLSLI
