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

// 間接拡散光の出どころによる compile-time 分割。光照贴图の面（実例に光照贴图があれば全面が光照贴图）は探針を読む
// 命令を持たない PS で描く（読まない分岐でも命令があるだけで register を占め、前向きが 4 割遅かった）。
// AS は面の flag（HIKARI_SURFACE_GPU_SCENE_FLAG_LIGHTMAPPED）で振り分ける。
//   ANY      : 分割しない。
//   PROBES   : 光照贴图の無い面だけ（PS は ANY と同じ）。
//   LIGHTMAP : 光照贴图の面だけ。
//   MASS     : 標準材質の大量配置の面だけ。探針は mesh shader が meshlet ごとに読み、PS は読まない（森の視点で前向きの
//              約 3 割が探針の読み出しだった）。標準材質の PROBES はこの面を描かない。
#define HIKARI_SURFACE_LIGHTING_ANY 0
#define HIKARI_SURFACE_LIGHTING_PROBES 1
#define HIKARI_SURFACE_LIGHTING_LIGHTMAP 2
#define HIKARI_SURFACE_LIGHTING_MASS 3

#ifndef HIKARI_SURFACE_LIGHTING
#define HIKARI_SURFACE_LIGHTING HIKARI_SURFACE_LIGHTING_ANY
#endif

// 深度を書く pass（GeometryAux）の被覆による compile-time 分割。PS が画素を捨てうると提前深度が効かず、隠れた面まで
// 着色する。alpha test・材質の特効・材質 program（式の被覆、殻）を持たない面は捨てない PS で先に描き、捨てうる面は後で
// 描いて先の深度で隠れた画素を着色の前に落とす。AS は面の flag で振り分ける。
//   ANY    : 分割しない。
//   OPAQUE : 捨てない面だけ。
//   CLIP   : 捨てうる面だけ。
#define HIKARI_SURFACE_COVERAGE_ANY 0
#define HIKARI_SURFACE_COVERAGE_OPAQUE 1
#define HIKARI_SURFACE_COVERAGE_CLIP 2

#ifndef HIKARI_SURFACE_COVERAGE
#define HIKARI_SURFACE_COVERAGE HIKARI_SURFACE_COVERAGE_ANY
#endif

#endif
