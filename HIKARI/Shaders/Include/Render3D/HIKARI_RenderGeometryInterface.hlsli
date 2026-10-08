#ifndef HIKARI_RENDER_GEOMETRY_INTERFACE_HLSLI
#define HIKARI_RENDER_GEOMETRY_INTERFACE_HLSLI

#define HIKARI_RENDER_GEOMETRY_INTERFACE_VERSION 2
// Shared GPU surface tail: uniform shell settings; AS payload adds count/LOD amount.
#define HIKARI_SURFACE_GPU_SCENE_INSTANCE_BYTES 576
#define HIKARI_MATERIAL_SHELL_INTERFACE_VERSION 1
// 80 bytes: 3 float4, outerConeCos, angularFalloff, uint kind, sourceRadius, float4 shapeTangent.
#define HIKARI_LOCAL_LIGHT_RECORD_VERSION 4
// ShadowCB: directional prefix 352 bytes, local matrices 1024, local info 16.
#define HIKARI_SHADOW_RECEIVER_CONSTANT_BYTES 1392
// Sky/environment providers include the shared 64-byte cloud solar field.
#define HIKARI_SKY_ENVIRONMENT_CONSTANT_BYTES 752
#define HIKARI_CLOUD_SOLAR_CONSTANT_BYTES 64

#endif
