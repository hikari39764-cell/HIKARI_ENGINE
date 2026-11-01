#include "Render2D/HIKARI_Renderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Render2D/HIKARI_Camera.h"
#include "Render2D/HIKARI_DxRenderer.h"

namespace HIKARI::RENDERER {
namespace {

struct MeshQuadCommand {
    RenderLayer layer = RenderLayer::Entity;
    float sortY = 0.0f;
    int orderIndex = 0;
    CameraMode camMode = CameraMode::Inherit;
    unsigned int rgba = 0xFFFFFFFF;
    int dxHandle = -1;
    Vector2 lt{}, rt{}, lb{}, rb{};
    float u_lt = 0, v_lt = 0, u_rt = 0, v_rt = 0, u_lb = 0, v_lb = 0, u_rb = 0, v_rb = 0;
};

std::vector<MeshQuadCommand> gRenderList;
RenderLayer gCurrentLayer = RenderLayer::Entity;
int gSubmissionCount = 0;

Vector2 TransformPoint(const Vector2& p, const Matrix3x3& m) {
    return {p.x * m.m[0][0] + p.y * m.m[1][0] + m.m[2][0], p.x * m.m[0][1] + p.y * m.m[1][1] + m.m[2][1]};
}

void Execute(const MeshQuadCommand& cmd) {
    // 頂点は既にワールド座標。カメラ継承時のみビュー行列を掛ける。
    const Matrix3x3 m = cmd.camMode == CameraMode::Ignore ? Matrix3x3::MakeIdentity() : CAMERA::GetViewMatrix();
    const Vector2 lt = TransformPoint(cmd.lt, m);
    const Vector2 rt = TransformPoint(cmd.rt, m);
    const Vector2 lb = TransformPoint(cmd.lb, m);
    const Vector2 rb = TransformPoint(cmd.rb, m);
    DX::DxRenderer::DrawMeshQuad(lt.x, lt.y, cmd.u_lt, cmd.v_lt, rt.x, rt.y, cmd.u_rt, cmd.v_rt, lb.x, lb.y, cmd.u_lb,
                                 cmd.v_lb, rb.x, rb.y, cmd.u_rb, cmd.v_rb, cmd.dxHandle, cmd.rgba);
}

} // namespace

void SetCurrentLayer(RenderLayer layer) { gCurrentLayer = layer; }
RenderLayer GetCurrentLayer() { return gCurrentLayer; }

void BeginFrame() {
    gRenderList.clear();
    gRenderList.reserve(1024);
    gSubmissionCount = 0;
    gCurrentLayer = RenderLayer::Entity;
}

void RenderLayerRange(RenderLayer minLayer, RenderLayer maxLayer, bool clearAfter) {
    if (gRenderList.empty()) {
        return;
    }
    std::sort(gRenderList.begin(), gRenderList.end(), [](const MeshQuadCommand& a, const MeshQuadCommand& b) {
        if (a.layer != b.layer) {
            return a.layer < b.layer;
        }
        if (std::abs(a.sortY - b.sortY) > 0.1f) {
            return a.sortY < b.sortY;
        }
        return a.orderIndex < b.orderIndex;
    });
    for (const MeshQuadCommand& cmd : gRenderList) {
        if (cmd.layer >= minLayer && cmd.layer <= maxLayer) {
            Execute(cmd);
        }
    }
    if (clearAfter) {
        ClearSubmittedCommands();
    }
}

void ClearSubmittedCommands() {
    gRenderList.clear();
    gSubmissionCount = 0;
}

void DrawMeshQuadHandleUV_Vertices(int textureHandle, const Vector2& lt, const Vector2& rt, const Vector2& lb,
                                   const Vector2& rb, float u_lt, float v_lt, float u_rt, float v_rt, float u_lb,
                                   float v_lb, float u_rb, float v_rb, CameraMode cam, unsigned int rgba) {
    if (textureHandle < 0) {
        return;
    }
    // 底辺側の大きい Y で奥行きソートする（画面手前ほど後に描く）
    gRenderList.push_back({gCurrentLayer, (std::max)(lb.y, rb.y), gSubmissionCount++, cam, rgba, textureHandle, lt, rt,
                           lb, rb, u_lt, v_lt, u_rt, v_rt, u_lb, v_lb, u_rb, v_rb});
}

} // namespace HIKARI::RENDERER
