#ifndef HIKARI_FORWARD_VERTEX_META_INCLUDED
#define HIKARI_FORWARD_VERTEX_META_INCLUDED

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
