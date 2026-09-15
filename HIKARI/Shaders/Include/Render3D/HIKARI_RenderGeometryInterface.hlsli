#ifndef HIKARI_RENDER_GEOMETRY_INTERFACE_HLSLI
#define HIKARI_RENDER_GEOMETRY_INTERFACE_HLSLI

#define HIKARI_RENDER_GEOMETRY_INTERFACE_VERSION 1
// 64 bytes: 3 float4, outerConeCos, angularFalloff, uint kind, sourceRadius.
#define HIKARI_LOCAL_LIGHT_RECORD_VERSION 3
// ShadowCB: directional prefix 352 bytes, local matrices 1024, local info 16.
#define HIKARI_SHADOW_RECEIVER_CONSTANT_BYTES 1392
// Sky/environment providers include the shared 64-byte cloud solar field.
#define HIKARI_SKY_ENVIRONMENT_CONSTANT_BYTES 752
#define HIKARI_CLOUD_SOLAR_CONSTANT_BYTES 64

#endif
