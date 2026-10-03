// Mesh Shader 本線は一般面と地形を別 PSO で描く。catalog の define で地形版を作る。
#ifndef HIKARI_SURFACE_PARTITION
#define HIKARI_SURFACE_PARTITION 1
#endif
#define HIKARI_GEOMETRY_AUX_MESHLET_INPUT 1
#include "Render3D_GeometryAuxPS.hlsl"
