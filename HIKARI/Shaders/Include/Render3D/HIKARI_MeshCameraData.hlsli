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
    // Orthographic clip W is constant; recover the forward axis from depth's
    // gradient instead. No duplicated camera constants are needed.
    if (dot(gViewProj[3].xyz,gViewProj[3].xyz) < 1.0e-12f)
        return max(dot(worldPosition-gCameraPos.xyz,normalize(gViewProj[2].xyz)),0.0f);
    // Camera3D の RH perspective では clip W == -view Z。画面位置によらない軸深度。
    return max(mul(gViewProj, float4(worldPosition, 1.0f)).w, 0.0f);
}

float3 HikariCameraViewDirection(float3 worldPosition) {
    if (dot(gViewProj[3].xyz,gViewProj[3].xyz) < 1.0e-12f) return -normalize(gViewProj[2].xyz);
    const float3 value=gCameraPos.xyz-worldPosition;
    return dot(value,value)>1.0e-12f ? normalize(value) : float3(0,0,1);
}
#endif
