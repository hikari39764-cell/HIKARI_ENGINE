#ifndef HIKARI_FORWARD_VERTEX_META_INCLUDED
#define HIKARI_FORWARD_VERTEX_META_INCLUDED

// DXIL は宣言順で hardware register を割り当てる。未使用の field も省かない。
// SV_IsFrontFace は rasterizer 由来なので、VS/MS 出力には含めず PS 入力にだけ付加。
// このローカル宣言 macro は二つの型を定義した直後に解除する。
#define HIKARI_FORWARD_INTERPOLANTS \
    float4 position : SV_POSITION; \
    float3 worldPosWS : TEXCOORD1; \
    float3 normalWS : NORMAL; \
    float4 tangentWS : TANGENT; \
    float2 uv : TEXCOORD0; \
    float2 uv1 : TEXCOORD10; \
    nointerpolation uint materialDataIndex : TEXCOORD2; \
    nointerpolation uint forwardMeta : TEXCOORD3; \
    nointerpolation uint objectDataIndex : TEXCOORD4; \
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5; \
    nointerpolation uint debugSurfaceId : TEXCOORD6; \
    float3 characterRightWS : TEXCOORD8; \
    float3 characterForwardWS : TEXCOORD9; \
    nointerpolation uint surfaceFeatureFlags : TEXCOORD7; \
    float3 surfaceFeatureData : TEXCOORD11; \
    nointerpolation float4 shellLayer : TEXCOORD12;

struct HikariForwardVertexOutput
{
    HIKARI_FORWARD_INTERPOLANTS
};
struct HikariForwardPixelInput
{
    HIKARI_FORWARD_INTERPOLANTS
    bool isFrontFace : SV_IsFrontFace;
};
#undef HIKARI_FORWARD_INTERPOLANTS

// Forward 頂点契約の nointerpolation メタデータ 1 dword。
// パラメータキャッシュ圧を抑えるため、1bit/数bit のフラグと debug 用
// cluster id を 1 dword に詰める。VS/MS 側は Pack、PS 側は各アクセサで
// 取り出す。レイアウトを変える場合は全 forward VS/MS/PS を同時に更新
// すること。
//   bit  0     : receiveShadow
//   bits 1-2   : drawBucket
//   bits 4-7   : lodIndex
//   bits 8-31  : clusterId (debug 表示用途。24bit に切り詰め)
uint HikariPackForwardVertexMeta(
    uint receiveShadow,
    uint drawBucket,
    uint lodIndex,
    uint clusterId)
{
    return (receiveShadow & 0x1u) |
        ((drawBucket & 0x3u) << 1u) |
        ((lodIndex & 0xfu) << 4u) |
        (clusterId << 8u);
}

uint HikariForwardVertexMetaReceiveShadow(uint meta)
{
    return meta & 0x1u;
}

uint HikariForwardVertexMetaDrawBucket(uint meta)
{
    return (meta >> 1u) & 0x3u;
}

uint HikariForwardVertexMetaLodIndex(uint meta)
{
    return (meta >> 4u) & 0xfu;
}

uint HikariForwardVertexMetaClusterId(uint meta)
{
    return meta >> 8u;
}

#endif
