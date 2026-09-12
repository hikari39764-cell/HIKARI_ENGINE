#ifndef HIKARI_MESH_CAMERA_DATA_INCLUDED
#define HIKARI_MESH_CAMERA_DATA_INCLUDED

// C++ CameraCB と同一の順序。Forward、SceneSurface、VS/MS で共有する。
cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gSimulationTimeParams;
    float4 gScreenParams;
};

#endif
