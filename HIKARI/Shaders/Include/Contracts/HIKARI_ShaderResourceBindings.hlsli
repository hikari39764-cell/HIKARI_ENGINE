#ifndef HIKARI_SHADER_RESOURCE_BINDINGS_INCLUDED
#define HIKARI_SHADER_RESOURCE_BINDINGS_INCLUDED

// Shader-side view of the global descriptor layout. Keep these values in sync
// with GFX::DESCRIPTOR in HIKARI_DescriptorHeapLayout.h.
static const uint HIKARI_SHADER_USER_SRV_COUNT = 3968u;
static const uint HIKARI_SHADER_SYSTEM_SRV_DYNAMIC_BEGIN = 4010u;
static const uint HIKARI_SHADER_SYSTEM_SRV_DYNAMIC_COUNT = 111u;

// Forward / mesh draw root binding contract.
static const uint HIKARI_SHADER_REG_B_CAMERA = 0u;
static const uint HIKARI_SHADER_REG_B_OBJECT = 1u;
static const uint HIKARI_SHADER_REG_B_LIGHT = 2u;
static const uint HIKARI_SHADER_REG_B_JOINT_PALETTE = 3u;
static const uint HIKARI_SHADER_REG_B_SHADOW = 4u;
static const uint HIKARI_SHADER_REG_B_SKY_ENVIRONMENT = 5u;
static const uint HIKARI_SHADER_REG_B_OBJECT_INDEX = 6u;
static const uint HIKARI_SHADER_REG_B_MATERIAL_INDEX = 7u;
static const uint HIKARI_SHADER_REG_B_SURFACE_GPU_SCENE_CONTROL = 8u;
static const uint HIKARI_SHADER_REG_B_CULLING_CAMERA = 9u;

static const uint HIKARI_SHADER_REG_T_SHADOW_MAP = 2u;
static const uint HIKARI_SHADER_REG_T_SKY_CUBE = 6u;
static const uint HIKARI_SHADER_REG_T_SCENE_DEPTH = 7u;
static const uint HIKARI_SHADER_REG_T_SCENE_COLOR = 8u;
static const uint HIKARI_SHADER_REG_T_IBL_IRRADIANCE = 9u;
static const uint HIKARI_SHADER_REG_T_IBL_PREFILTERED = 10u;
static const uint HIKARI_SHADER_REG_T_IBL_BRDF_LUT = 11u;
static const uint HIKARI_SHADER_REG_T_REFLECTION_PROBE_PREFILTERED = 12u;
static const uint HIKARI_SHADER_REG_T_SSAO = 13u;
// t14 (space0) は旧 light probe SH buffer の空き。SH volume は space2 の t0-t8。
static const uint HIKARI_SHADER_REG_T_OBJECT_DATA = 15u;
static const uint HIKARI_SHADER_REG_T_MATERIAL_DATA = 16u;
static const uint HIKARI_SHADER_REG_T_SURFACE_GPU_SCENE = 17u;
static const uint HIKARI_SHADER_REG_T_MESHLET_VISIBLE_RANGES = 18u;
static const uint HIKARI_SHADER_REG_T_MESHLET_VISIBLE_CLUSTER_LIST = 19u;
static const uint HIKARI_SHADER_REG_T_MATERIAL_TEXTURE_POOL = 20u;

static const uint HIKARI_SHADER_REG_SPACE_DEFAULT = 0u;
static const uint HIKARI_SHADER_REG_SPACE_CLUSTER_GEOMETRY = 1u;
static const uint HIKARI_SHADER_REG_SPACE_LIGHT_PROBE_SH_VOLUME = 2u;
static const uint HIKARI_SHADER_REG_T_LIGHT_PROBE_SH_VOLUME_FIRST = 0u;
static const uint HIKARI_SHADER_REG_T_LIGHT_PROBE_SH_VOLUME_COUNT = 9u;
static const uint HIKARI_SHADER_REG_T_CLUSTER_GEOMETRY_POOL = 0u;

static const uint HIKARI_SHADER_REG_S_LINEAR_WRAP = 0u;
static const uint HIKARI_SHADER_REG_S_SHADOW = 1u;

#endif
