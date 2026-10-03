#ifndef HIKARI_SURFACE_PARTITION_INCLUDED
#define HIKARI_SURFACE_PARTITION_INCLUDED

// 一般面と地形を別 PSO で描くための compile-time 分割。AS は同じ値で
// 可視範囲を振り分け、PS は担当外の着色経路を持たずに済む。
//   ALL     : 全経路 (水面・地形・一般面)。分割しない pass と診断表示用。
//   GENERAL : 地形と水面を除く一般面。
//   TERRAIN : 地形のみ。
#define HIKARI_SURFACE_PARTITION_ALL 0
#define HIKARI_SURFACE_PARTITION_GENERAL 1
#define HIKARI_SURFACE_PARTITION_TERRAIN 2

#ifndef HIKARI_SURFACE_PARTITION
#define HIKARI_SURFACE_PARTITION HIKARI_SURFACE_PARTITION_ALL
#endif

#endif
