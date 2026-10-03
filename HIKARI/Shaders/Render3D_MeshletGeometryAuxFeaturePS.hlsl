// Mesh Shader 本線は一般面と地形を別 PSO で描く。catalog の define で地形版を作る。
#ifndef HIKARI_SURFACE_PARTITION
#define HIKARI_SURFACE_PARTITION 1
#endif
#define HIKARI_GEOMETRY_AUX_WRITE_FEATURE_MASK 1
#include "Include/Render3D/HIKARI_RenderGeometryInterface.hlsli"
#include "Render3D_MeshletGeometryAuxPS.hlsl"
