#ifndef HIKARI_MESH_CAMERA_DATA_INCLUDED
#define HIKARI_MESH_CAMERA_DATA_INCLUDED

// C++ CameraCB と同一の順序。Forward、SceneSurface、VS/MS で共有する。
#ifndef HIKARI_CAMERA_CBUFFER_REGISTER
#define HIKARI_CAMERA_CBUFFER_REGISTER b0
#endif
cbuffer CameraCB : register(HIKARI_CAMERA_CBUFFER_REGISTER)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gSimulationTimeParams;
    float4 gScreenParams;
};

float HikariCameraViewDepth(float3 worldPosition)
{
    // Camera3D の RH perspective では clip W == -view Z。画面位置によらない軸深度。
    return max(mul(gViewProj, float4(worldPosition, 1.0f)).w, 0.0f);
}

#endif
