#ifndef HIKARI_RAY_SAMPLING_INCLUDED
#define HIKARI_RAY_SAMPLING_INCLUDED

// 光線の標本化に共通の乱数と方向の分布（光線追跡の反射、路径追跡の参照、光照贴图の烘焙）。

// PCG の整数ハッシュ（Jarzynski & Olano 2020）。
uint HikariPcgHash(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// 状態を進めて [0, 1) の一様乱数（24 bit）を返す。
float HikariRandom01(inout uint state)
{
    state = HikariPcgHash(state);
    return float(state & 0x00ffffffu) / 16777216.0f;
}

// 単位法線まわりの正規直交基底。
void HikariBuildOrthonormalBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    const float3 helper = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(helper, normal));
    bitangent = cross(normal, tangent);
}

// 余弦重みの半球（pdf = cos / π）。xi は [0, 1)^2。
float3 HikariSampleCosineHemisphere(float3 normal, float2 xi)
{
    float3 tangent;
    float3 bitangent;
    HikariBuildOrthonormalBasis(normal, tangent, bitangent);
    const float radius = sqrt(xi.x);
    const float angle = 6.28318530718f * xi.y;
    return tangent * (radius * cos(angle)) + bitangent * (radius * sin(angle)) +
        normal * sqrt(max(0.0f, 1.0f - xi.x));
}

// GGX の見える法線を球冠で標本化し（Dupuy & Benyoub 2023）、その法線での反射の向きを返す。
float3 HikariSampleGgxReflection(float3 normal, float3 view, float roughness, float2 xi)
{
    const float alpha = max(roughness * roughness, 1.0e-3f);
    float3 tangent;
    float3 bitangent;
    HikariBuildOrthonormalBasis(normal, tangent, bitangent);
    const float3 localView = float3(dot(view, tangent), dot(view, bitangent), dot(view, normal));
    const float3 stretched = normalize(float3(localView.xy * alpha, localView.z));
    const float phi = 6.28318530718f * xi.x;
    const float z = (1.0f - xi.y) * (1.0f + stretched.z) - stretched.z;
    const float sinTheta = sqrt(saturate(1.0f - z * z));
    const float3 cap = float3(sinTheta * cos(phi), sinTheta * sin(phi), z) + stretched;
    const float3 localHalf = normalize(float3(cap.xy * alpha, max(cap.z, 0.0f)));
    return reflect(-view, tangent * localHalf.x + bitangent * localHalf.y + normal * localHalf.z);
}

#endif
