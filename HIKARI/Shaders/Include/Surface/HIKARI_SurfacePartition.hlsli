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

// 材質の種類による compile-time 分割。標準 PBR（program 0）の面は式の解釈と風格化の経路を持たない PS で描き、
// 大きな PS の register 圧で標準の面まで遅くならないようにする。AS は面の flag で振り分ける。
//   ANY      : 分割しない。
//   STANDARD : 標準 PBR の材質だけ。
//   PROGRAM  : 材質 program（式・風格化）を持つ材質だけ。
#define HIKARI_MATERIAL_CLASS_ANY 0
#define HIKARI_MATERIAL_CLASS_STANDARD 1
#define HIKARI_MATERIAL_CLASS_PROGRAM 2

#ifndef HIKARI_MATERIAL_CLASS
#define HIKARI_MATERIAL_CLASS HIKARI_MATERIAL_CLASS_ANY
#endif

#endif
