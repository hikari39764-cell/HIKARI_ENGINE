#include "Render2D/HIKARI_Renderer.h"
#include <cmath>
#include "Render2D/HIKARI_DxRenderer.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "Windows.h"
#include <algorithm>
#undef max
#undef min
// ===== 蜀・Κ繝・・繝ｫ =====
namespace {

    using namespace HIKARI;
    using namespace HIKARI::RENDERER;

    static unsigned int gDefaultColor = 0xFFFFFFFF; // RGBA
    static Vector2 gOcclusionScreenPos = { 0.0f, 0.0f };
    static float gOcclusionRadius = 0.0f;

    static int gWhiteTexHandle = -1;
    static const char* kDefaultWhiteTexPath = "./HIKARI/white1x1.png";
    static std::string gWhiteTexPath = kDefaultWhiteTexPath;

    enum class CommandType {
        Sprite,
        Line,
        Box,
        Triangle,
        Ellipse,
        MeshQuad,
        DeformGrid
    };

    struct RenderCommand {

        RenderLayer layer;
        float sortY;
        int orderIndex;
        bool enableYSort = true;

        CommandType type;
        CameraMode camMode;
        unsigned int rgba;
        HIKARI::RENDERER::FillMode fillMode;

        int dxHandle;

        HIKARI::Transform2D transform;
        float width, height;

        SpriteUV uv;

        float u_lt, v_lt, u_rt, v_rt, u_lb, v_lb, u_rb, v_rb;

        Vector2 p0, p1, p2, p3;

        float radiusX, radiusY;

        DeformGrid gridData;
        bool useMaskShader = false;
    };

    static std::vector<RenderCommand> gRenderList;
    static RenderLayer gCurrentLayer = RenderLayer::Entity;
    static int gSubmissionCount = 0;

    static inline int EnsureWhiteTexture() {
        if (gWhiteTexHandle < 0) {
            // 逕ｨ DX 郤ｹ逅・ｮ｡逅・勣霓ｽ蜈･ 1x1 逋ｽ雍ｴ蝗ｾ
            gWhiteTexHandle = HIKARI::DXTEX::DxTextureManager::LoadTexture(
                "Renderer_White1x1",
                gWhiteTexPath
            );
        }
        return gWhiteTexHandle;
    }


    static inline Vector2 TransformPoint(const Vector2& p, const Matrix3x3& m) {
        Vector2 o{};
        o.x = p.x * m.m[0][0] + p.y * m.m[1][0] + m.m[2][0];
        o.y = p.x * m.m[0][1] + p.y * m.m[1][1] + m.m[2][1];
        return o;
    }

    static inline Matrix3x3 ComposeWorldThenMaybeView(const Matrix3x3& world, CameraMode cam) {
        if (cam == CameraMode::Ignore) return world;
        const Matrix3x3& view = HIKARI::CAMERA::GetViewMatrix();
        return world * view;
    }

    static void ExecuteDrawCommand(const RenderCommand& cmd)
    {
        Matrix3x3 world = Matrix3x3::MakeIdentity();

        // [菫ｮ螟江・夂ｧｻ髯､莠・MeshQuad 逧・ｷｳ霑・ｻ霎・
        // 莉･蜑搾ｼ喨f (cmd.type != CommandType::Line && cmd.type != CommandType::MeshQuad ...
        // 邇ｰ蝨ｨ・壼ｿ・｡ｻ蜈∬ｮｸ MeshQuad 隶｡邂・World 遏ｩ髦ｵ・悟凄蛻・Spine 譌豕戊ｷ滄囂 Transform 遘ｻ蜉ｨ
        if (cmd.type != CommandType::Line && cmd.type != CommandType::DeformGrid) {
            world = cmd.transform.ToWorld(cmd.width, cmd.height);
        }

        Matrix3x3 m = ComposeWorldThenMaybeView(world, cmd.camMode);

        switch (cmd.type)
        {
        case CommandType::Sprite:
        {
            if (cmd.dxHandle < 0) return;
            Vector2 pLT{ 0.0f, 0.0f };
            Vector2 pRT{ cmd.width, 0.0f };
            Vector2 pLB{ 0.0f, cmd.height };
            Vector2 pRB{ cmd.width, cmd.height };

            Vector2 lt = TransformPoint(pLT, m);
            Vector2 rt = TransformPoint(pRT, m);
            Vector2 lb = TransformPoint(pLB, m);
            Vector2 rb = TransformPoint(pRB, m);

            if (cmd.useMaskShader) {
                HIKARI::DX::DxRenderer::DrawMeshQuadMasked(
                    lt.x, lt.y, cmd.uv.u0, cmd.uv.v0,
                    rt.x, rt.y, cmd.uv.u1, cmd.uv.v0,
                    lb.x, lb.y, cmd.uv.u0, cmd.uv.v1,
                    rb.x, rb.y, cmd.uv.u1, cmd.uv.v1,
                    cmd.dxHandle, cmd.rgba
                );
            } else {
                HIKARI::DX::DxRenderer::DrawMeshQuad(
                    lt.x, lt.y, cmd.uv.u0, cmd.uv.v0,
                    rt.x, rt.y, cmd.uv.u1, cmd.uv.v0,
                    lb.x, lb.y, cmd.uv.u0, cmd.uv.v1,
                    rb.x, rb.y, cmd.uv.u1, cmd.uv.v1,
                    cmd.dxHandle, cmd.rgba
                );
            }


            break;
        }
        case CommandType::Line:
        {

            Vector2 a = TransformPoint(cmd.p0, m);
            Vector2 b = TransformPoint(cmd.p1, m);
            HIKARI::DX::DxRenderer::DrawLine((float)a.x, (float)a.y, (float)b.x, (float)b.y, cmd.rgba);
            break;
        }
        case CommandType::Box:
        {
            // Wireframe Box: 逕ｻ4譚｡郤ｿ
            // Fill Box: 荵句燕謌台ｻｬ蝨ｨ Push 髦ｶ谿ｵ蛛ｷ諛定ｽｬ謌蝉ｺ・Sprite・梧園莉･霑咎㈹蜿ｪ螟・炊 Wireframe
            if (cmd.fillMode == HIKARI::RENDERER::FillMode::Wireframe) {
                Vector2 pLT{ 0.0f, 0.0f };
                Vector2 pRT{ cmd.width, 0.0f };
                Vector2 pLB{ 0.0f, cmd.height };
                Vector2 pRB{ cmd.width, cmd.height };

                Vector2 lt = TransformPoint(pLT, m);
                Vector2 rt = TransformPoint(pRT, m);
                Vector2 lb = TransformPoint(pLB, m);
                Vector2 rb = TransformPoint(pRB, m);

                HIKARI::DX::DxRenderer::DrawLine(lt.x, lt.y, rt.x, rt.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(rt.x, rt.y, rb.x, rb.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(rb.x, rb.y, lb.x, lb.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(lb.x, lb.y, lt.x, lt.y, cmd.rgba);
            }
            break;
        }
        case CommandType::Triangle:
        {
            // p0, p1, p2 譏ｯ逶ｸ蟇ｹ莠・Transform 逧・ｱ驛ｨ蝮先・
            // 蜴滓磁蜿｣譏ｯ DrawTriangle(t, p0, p1, p2)
            Vector2 a = TransformPoint(cmd.p0, m);
            Vector2 b = TransformPoint(cmd.p1, m);
            Vector2 c = TransformPoint(cmd.p2, m);

            if (cmd.fillMode == HIKARI::RENDERER::FillMode::Wireframe) {
                HIKARI::DX::DxRenderer::DrawLine(a.x, a.y, b.x, b.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(b.x, b.y, c.x, c.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(c.x, c.y, a.x, a.y, cmd.rgba);
            } else {
                // Fill Triangle: 菴ｿ逕ｨ逋ｽ蝗ｾ逕ｻ Mesh
                if (cmd.dxHandle >= 0) {
                    // 蛻ｩ逕ｨ邂蟷ｶ蝗幄ｾｹ蠖｢逕ｻ荳芽ｧ貞ｽ｢ (v3 = v2)
                    HIKARI::DX::DxRenderer::DrawMeshQuad(
                        a.x, a.y, 0, 0,
                        b.x, b.y, 1, 0,
                        c.x, c.y, 0, 1,
                        c.x, c.y, 1, 1, // 驥榊､咲せ
                        cmd.dxHandle, cmd.rgba
                    );
                }
            }
            break;
        }
        case CommandType::Ellipse:
        {
            const int kSegments = 32; // 遞榊ｾｮ髯堺ｽ惹ｸ轤ｹ谿ｵ謨ｰ莉･謠宣ｫ俶ｧ閭ｽ
            float cx = cmd.width * 0.5f;
            float cy = cmd.height * 0.5f;
            float rx = cmd.radiusX;
            float ry = cmd.radiusY;

            std::vector<Vector2> pts;
            pts.reserve(kSegments + 1);

            for (int i = 0; i <= kSegments; ++i) {
                float theta = (float)i / (float)kSegments * 6.2831853f;
                Vector2 pLocal{ cx + rx * std::cos(theta), cy + ry * std::sin(theta) };
                pts.push_back(TransformPoint(pLocal, m));
            }

            if (cmd.fillMode == HIKARI::RENDERER::FillMode::Wireframe) {
                for (int i = 0; i < kSegments; ++i) {
                    HIKARI::DX::DxRenderer::DrawLine(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, cmd.rgba);
                }
            } else {
                if (cmd.dxHandle >= 0) {
                    Vector2 centerWorld = TransformPoint({ cx, cy }, m);
                    // Fill: Triangle Fan
                    for (int i = 0; i < kSegments; ++i) {
                        HIKARI::DX::DxRenderer::DrawMeshQuad(
                            centerWorld.x, centerWorld.y, 0.5f, 0.5f,
                            pts[i].x, pts[i].y, 1, 0,
                            pts[i + 1].x, pts[i + 1].y, 0, 1,
                            pts[i + 1].x, pts[i + 1].y, 0, 1,
                            cmd.dxHandle, cmd.rgba
                        );
                    }
                }
            }
            break;
        }
        case CommandType::MeshQuad:
        {
            if (cmd.dxHandle < 0) return;

            // 霑咎㈹菴ｿ逕ｨ隶｡邂怜･ｽ逧・m 遏ｩ髦ｵ (World * View) 霑幄｡悟序謐｢
            // DrawMeshQuadHandleUV_Local 莨蜈･莠・transform・梧園莉･ m 蛹・性莠・ｯ･ transform
            // DrawMeshQuadHandleUV_Vertices 莨蜈･莠・Identity・梧園莉･ m 莉・桁蜷ｫ View
            Vector2 lt = TransformPoint(cmd.p0, m); // p0=lt
            Vector2 rt = TransformPoint(cmd.p1, m); // p1=rt
            Vector2 lb = TransformPoint(cmd.p2, m); // p2=lb
            Vector2 rb = TransformPoint(cmd.p3, m); // p3=rb

            HIKARI::DX::DxRenderer::DrawMeshQuad(
                lt.x, lt.y, cmd.u_lt, cmd.v_lt,
                rt.x, rt.y, cmd.u_rt, cmd.v_rt,
                lb.x, lb.y, cmd.u_lb, cmd.v_lb,
                rb.x, rb.y, cmd.u_rb, cmd.v_rb,
                cmd.dxHandle,
                cmd.rgba
            );
            break;
        }

        case CommandType::DeformGrid:
        {
            if (cmd.dxHandle < 0) return;
            const auto& grid = cmd.gridData;
            if (grid.cols < 2 || grid.rows < 2) return;

            int vertexCount = grid.cols * grid.rows;
            if (grid.positions.size() != vertexCount) return;

            static std::vector<Vector2> tempTransformed; // 驕ｿ蜈榊渚螟榊・驟榊・蟄・
            if (tempTransformed.size() < vertexCount) tempTransformed.resize(vertexCount);

            for (int i = 0; i < vertexCount; ++i) {
                tempTransformed[i] = TransformPoint(grid.positions[i], m);
            }

            bool hasUV = (grid.uvs.size() == vertexCount);

            for (int y = 0; y < grid.rows - 1; ++y) {
                for (int x = 0; x < grid.cols - 1; ++x) {
                    int i0 = y * grid.cols + x;
                    int i1 = y * grid.cols + (x + 1);
                    int i2 = (y + 1) * grid.cols + x;
                    int i3 = (y + 1) * grid.cols + (x + 1);

                    HIKARI::DX::MeshVertex verts[4]{};
                    verts[0].px = tempTransformed[i0].x; verts[0].py = tempTransformed[i0].y;
                    verts[1].px = tempTransformed[i1].x; verts[1].py = tempTransformed[i1].y;
                    verts[2].px = tempTransformed[i2].x; verts[2].py = tempTransformed[i2].y;
                    verts[3].px = tempTransformed[i3].x; verts[3].py = tempTransformed[i3].y;

                    if (hasUV) {
                        verts[0].u = grid.uvs[i0].x; verts[0].v = grid.uvs[i0].y;
                        verts[1].u = grid.uvs[i1].x; verts[1].v = grid.uvs[i1].y;
                        verts[2].u = grid.uvs[i2].x; verts[2].v = grid.uvs[i2].y;
                        verts[3].u = grid.uvs[i3].x; verts[3].v = grid.uvs[i3].y;
                    } else {
                        // fallback UV
                        float u0 = (float)x / (grid.cols - 1); float v0 = (float)y / (grid.rows - 1);
                        float u1 = (float)(x + 1) / (grid.cols - 1); float v1 = (float)(y + 1) / (grid.rows - 1);
                        verts[0].u = u0; verts[0].v = v0; verts[1].u = u1; verts[1].v = v0;
                        verts[2].u = u0; verts[2].v = v1; verts[3].u = u1; verts[3].v = v1;
                    }

                    HIKARI::DX::DxRenderer::DrawMesh(verts, 4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, cmd.dxHandle, cmd.rgba);
                }
            }
            break;
        }
        }
    }



} // anonymous

namespace HIKARI {
    namespace RENDERER {


        void SetCurrentLayer(RenderLayer layer) { gCurrentLayer = layer; }
        RenderLayer GetCurrentLayer() { return gCurrentLayer; }

        void BeginFrame() {
            gRenderList.clear();
            if (gRenderList.capacity() < 1024) {
                gRenderList.reserve(1024);
            }
            gSubmissionCount = 0;
            gCurrentLayer = RenderLayer::Entity;
        }


        void RenderAll() {
            RenderLayerRange(RenderLayer::Background, RenderLayer::Debug, true);
        }

        void RenderLayerRange(RenderLayer minLayer, RenderLayer maxLayer, bool clearAfter) {
            if (gRenderList.empty()) return;

            std::sort(gRenderList.begin(), gRenderList.end(),
                [](const RenderCommand& a, const RenderCommand& b) {
                    if (a.layer != b.layer) return (int)a.layer < (int)b.layer;
                    const bool useY = a.enableYSort && b.enableYSort;
                    if (useY && std::abs(a.sortY - b.sortY) > 0.1f) {
                        return a.sortY < b.sortY;
                    }
                    return a.orderIndex < b.orderIndex;
                });

            for (const auto& cmd : gRenderList) {
                if ((int)cmd.layer < (int)minLayer || (int)cmd.layer > (int)maxLayer) {
                    continue;
                }
                ExecuteDrawCommand(cmd);
            }

            if (clearAfter) {
                gRenderList.clear();
                gSubmissionCount = 0;
            }
        }

        void ClearSubmittedCommands() {
            gRenderList.clear();
            gSubmissionCount = 0;
        }

        void SetDefaultColor(unsigned int rgba) { gDefaultColor = rgba; }
        unsigned int GetDefaultColor() { return gDefaultColor; }
        void ReserveRenderCommands(size_t count) { gRenderList.reserve(count); }

        void SetBlendMode(BlendMode mode)
        {
            HIKARI::DX::DxRenderer::SetBlendMode(static_cast<HIKARI::DX::BlendMode>(mode));
        }

        void SetWhiteTexturePath(const char* pathRGBA1x1) {
            if (pathRGBA1x1 && pathRGBA1x1[0] != '\0') {
                gWhiteTexPath = pathRGBA1x1;
                gWhiteTexHandle = -1; // 谺｡蝗槫茜逕ｨ譎ゅ↓蜀阪Ο繝ｼ繝峨＆縺帙ｋ
            }
        }

        void SetOcclusionSource(Vector2 screenPos, float radius) {
            gOcclusionScreenPos = screenPos;
            gOcclusionRadius = radius;
            // 蜷梧慮譖ｴ譁ｰ蠎募ｱ・
            HIKARI::DX::DxRenderer::UpdateMaskParams(screenPos.x, screenPos.y, radius, 40.0f);
        }

        // ---- 蝓ｺ譛ｬ逧・↑邱壽緒逕ｻ ----
        void DrawLine(Vector2 p0, Vector2 p1, CameraMode cam, unsigned int rgba) {
            RenderCommand cmd;
            cmd.type = CommandType::Line;
            cmd.layer = gCurrentLayer;
            cmd.sortY = (p0.y + p1.y) * 0.5f;
            cmd.orderIndex = gSubmissionCount++;

            cmd.p0 = p0;
            cmd.p1 = p1;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // ---- Quad----
        void DrawQuad(const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam,
            unsigned int rgba,
            bool billboard) {
            DrawBox(t, width, height, FillMode::Fill, cam, rgba, billboard);
        }

        // ---- Box ----
        void DrawBox(const HIKARI::Transform2D& t, float width, float height, FillMode mode, CameraMode cam, unsigned int rgba, bool billboard) {
            unsigned int c = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            HIKARI::Transform2D tt = t;
            if (billboard && cam == CameraMode::Inherit) {
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            if (mode == FillMode::Fill) {
                int h = EnsureWhiteTexture();
                if (h >= 0) {
                    RenderCommand cmd;
                    cmd.type = CommandType::Sprite;
                    cmd.layer = gCurrentLayer;
                    cmd.sortY = t.position.y;
                    cmd.orderIndex = gSubmissionCount++;
                    cmd.dxHandle = h;
                    cmd.transform = tt;
                    cmd.width = width; cmd.height = height;
                    cmd.uv = { 0,0,1,1 };
                    cmd.camMode = cam;
                    cmd.rgba = c;
                    gRenderList.push_back(cmd);
                    return;
                }
                mode = FillMode::Wireframe;
            }

            // Wireframe
            RenderCommand cmd;
            cmd.type = CommandType::Box;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;
            cmd.transform = tt;
            cmd.width = width;
            cmd.height = height;
            cmd.fillMode = FillMode::Wireframe;
            cmd.camMode = cam;
            cmd.rgba = c;
            gRenderList.push_back(cmd);
        }

        void DrawTriangle(const HIKARI::Transform2D& t, Vector2 p0, Vector2 p1, Vector2 p2, FillMode mode, CameraMode cam, unsigned int rgba, bool billboard)
        {
            HIKARI::Transform2D tt = t;
            if (billboard && cam == CameraMode::Inherit) {
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::Triangle;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.transform = tt;
            cmd.width = 1.0f; cmd.height = 1.0f;

            cmd.p0 = p0; cmd.p1 = p1; cmd.p2 = p2;
            cmd.fillMode = mode;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            if (mode == FillMode::Fill) {
                cmd.dxHandle = EnsureWhiteTexture();
            }
            gRenderList.push_back(cmd);
        }


        void DrawEllipse(const HIKARI::Transform2D& t, float radiusX, float radiusY, FillMode mode, CameraMode cam, unsigned int rgba, bool billboard)
        {
            HIKARI::Transform2D tt = t;
            if (billboard && cam == CameraMode::Inherit) {
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::Ellipse;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.transform = tt;
            cmd.width = radiusX * 2.0f;
            cmd.height = radiusY * 2.0f;
            cmd.radiusX = radiusX;
            cmd.radiusY = radiusY;
            cmd.fillMode = mode;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            if (mode == FillMode::Fill) {
                cmd.dxHandle = EnsureWhiteTexture();
            }
            gRenderList.push_back(cmd);
        }

        // ---- 繧ｹ繝励Λ繧､繝茨ｼ壼錐蜑肴欠螳・

        void DrawSprite(const std::string& textureName, const HIKARI::Transform2D& t, float width, float height, CameraMode cam, unsigned int rgba)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite; // <--- 譬・ｮｰ邀ｻ蝙・
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = t;
            cmd.width = width;
            cmd.height = height;
            SpriteUV uv;
            uv.u0 = 0.0f;
            uv.v0 = 0.0f;
            uv.u1 = 1.0f;
            uv.v1 = 1.0f;
            cmd.uv = uv;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // ---- 繧ｹ繝励Λ繧､繝茨ｼ壼錐蜑肴欠螳・+ UV ----
        void DrawSprite(const std::string& textureName, const HIKARI::Transform2D& t, float width, float height, const SpriteUV& uv, CameraMode cam, unsigned int rgba)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite; // <--- 譬・ｮｰ邀ｻ蝙・
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = t;
            cmd.width = width;
            cmd.height = height;
            cmd.uv = uv;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        void DrawSpriteRect(const std::string& textureName,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam,
            unsigned int rgba)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            // 闔ｷ蜿也ｺｹ逅・ｰｺ蟇ｸ莉･隶｡邂・UV
            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(handle, texW, texH);
            if (texW == 0 || texH == 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = t;
            cmd.width = dstW;
            cmd.height = dstH;

            // 隶｡邂・UV
            cmd.uv.u0 = (float)srcX / texW;
            cmd.uv.v0 = (float)srcY / texH;
            cmd.uv.u1 = (float)(srcX + srcW) / texW;
            cmd.uv.v1 = (float)(srcY + srcH) / texH;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        void PushSpriteRectHandleEx(
            int textureHandle,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam,
            unsigned int rgba,
            float sortYOverride)
        {
            if (textureHandle < 0) {
                return;
            }

            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(textureHandle, texW, texH);
            if (texW == 0 || texH == 0) {
                return;
            }

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = sortYOverride;

            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;
            cmd.transform = t;
            cmd.width = dstW;
            cmd.height = dstH;

            cmd.uv.u0 = static_cast<float>(srcX) / texW;
            cmd.uv.v0 = static_cast<float>(srcY) / texH;
            cmd.uv.u1 = static_cast<float>(srcX + srcW) / texW;
            cmd.uv.v1 = static_cast<float>(srcY + srcH) / texH;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        void DrawSpriteBill(const std::string& textureName,
            const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam,
            unsigned int rgba,
            bool billboard, bool useMaskShader)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            // Transform縺ｮ繧ｳ繝斐・繧剃ｽ懈・縺励√ン繝ｫ繝懊・繝芽ｨ育ｮ励ｒ驕ｩ逕ｨ
            HIKARI::Transform2D tt = t;

            if (billboard && cam == CameraMode::Inherit) {
                // 1) 繧ｫ繝｡繝ｩ縺ｮ蝗櫁ｻ｢繧呈遠縺｡豸医☆・亥ｸｸ縺ｫ豁｣髱｢繧貞髄縺擾ｼ・
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                // 2) 繧ｫ繝｡繝ｩ縺ｮ繝斐ャ繝∬ｧ偵↓繧医ｋY霆ｸ縺ｮ邵ｮ縺ｿ繧定｣懈ｭ｣
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            // 繧ｽ繝ｼ繝磯・・雜ｳ蜈・・Y蠎ｧ讓・
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = tt; // 陬懈ｭ｣蠕後・Transform繧呈ｸ｡縺・
            cmd.width = width;
            cmd.height = height;

            // UV縺ｯ蜈ｨ菴・
            cmd.uv.u0 = 0.0f; cmd.uv.v0 = 0.0f;
            cmd.uv.u1 = 1.0f; cmd.uv.v1 = 1.0f;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);
            cmd.useMaskShader = useMaskShader;

            gRenderList.push_back(cmd);
        }

        void DrawSpriteRectBill(const std::string& textureName,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam,
            unsigned int rgba,
            bool billboard,
            bool useMaskShader)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(handle, texW, texH);
            if (texW == 0 || texH == 0) return;

            // Transform縺ｮ繧ｳ繝斐・繧剃ｽ懈・縺励√ン繝ｫ繝懊・繝芽ｨ育ｮ励ｒ驕ｩ逕ｨ
            HIKARI::Transform2D tt = t;

            if (billboard && cam == CameraMode::Inherit) {
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = tt; // 陬懈ｭ｣蠕後・Transform
            cmd.width = dstW;
            cmd.height = dstH;

            // UV險育ｮ・
            cmd.uv.u0 = (float)srcX / texW;
            cmd.uv.v0 = (float)srcY / texH;
            cmd.uv.u1 = (float)(srcX + srcW) / texW;
            cmd.uv.v1 = (float)(srcY + srcH) / texH;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            // 繧ｷ繧ｧ繝ｼ繝繝ｼ蛻・ｊ譖ｿ縺育畑繝輔Λ繧ｰ繧定ｨｭ螳・
            cmd.useMaskShader = useMaskShader;

            gRenderList.push_back(cmd);
        }


        void DrawSpritePileStrip(
            const SpritePileStrip& strip,
            const SpritePileConfig& cfg,
            const HIKARI::Transform2D& baseT,
            float baseZ,
            CameraMode cam)
        {
            if (strip.textureName.empty()) { return; }
            if (strip.sliceW <= 0 || strip.sliceH <= 0) { return; }
            if (strip.sliceCount <= 0) { return; }

            const float dstW = (cfg.dstW > 0.0f) ? cfg.dstW : (float)strip.sliceW;
            const float dstH = (cfg.dstH > 0.0f) ? cfg.dstH : (float)strip.sliceH;
            Vector2 pivotPx{ dstW * cfg.pivotX01, dstH * cfg.pivotY01 };

            float camRot = 0.0f;
            float camScaleY = 1.0f;
            float s = 0.0f;
            float c = 1.0f;

            if (cam == CameraMode::Inherit) {
                camRot = HIKARI::CAMERA::GetRotation();
                camScaleY = HIKARI::CAMERA::GetBillboardScaleY();
                s = std::sin(camRot);
                c = std::cos(camRot);
            }

            // 霎・勧蜃ｽ謨ｰ
            auto calculateOffset = [&](int i) -> Vector2 {
                Vector2 offset{ 0.0f, 0.0f };

                // 蜴壼ｺｦ蛛冗ｧｻ
                float totalYOffset = 0.0f;
                if (cfg.depthMode == SpritePileDepthMode::Pure2D_YStep) {
                    if (cfg.yStepPx != 0.0f) {
                        totalYOffset += (float)i * cfg.yStepPx * camScaleY;
                    }
                } else if (cfg.extraYStepPx != 0.0f) {
                    totalYOffset += (float)i * cfg.extraYStepPx * camScaleY;
                }

                if (totalYOffset != 0.0f) {

                    offset.x -= totalYOffset * s;
                    offset.y -= totalYOffset * c;
                }

                if (cfg.depthMode == SpritePileDepthMode::Pure2D_YStep) {

                    if (cfg.stepXPerSlicePx != 0.0f) offset.x += (float)i * cfg.stepXPerSlicePx;
                    if (cfg.stepYPerSlicePx != 0.0f) offset.y += (float)i * cfg.stepYPerSlicePx;
                }

                return offset;
                };

            // 螳樣刔扈伜宛蜃ｽ謨ｰ
            auto drawCmd = [&](int srcX, int srcY, const HIKARI::Transform2D& drawT) {
                int handle = HIKARI::TEXTURE::GetDxHandle(strip.textureName);
                if (handle < 0) return;

                // 謗貞ｺ乗ｰｸ霑懃畑迚ｩ菴楢・蠎慕噪 Y縲・
                // baseZ 逕ｨ菴懌懈賜蠎剰｡･蛛ｿ窶晢ｼ井ｾ句ｦりｷｳ霍・ｼ夊ｧ・ｧ我ｸ・y 蠕荳頑堪・御ｽ・賜蠎丈ｻ肴潔閼壼ｺ・y・峨・
                float pileSortY = baseT.position.y + baseZ;

                PushSpriteRectHandleEx(
                    handle,
                    srcX, srcY, strip.sliceW, strip.sliceH,
                    drawT,
                    dstW, dstH,
                    cam,
                    cfg.rgba,
                    pileSortY
                );
                };

            struct DrawInfo {
                int sliceIndex;
                Vector2 finalPos;
            };
            static std::vector<DrawInfo> drawQueue;
            drawQueue.clear();
            drawQueue.reserve(strip.sliceCount * (cfg.gapFillSteps + 1));

            int steps = (cfg.gapFillSteps < 0) ? 0 : cfg.gapFillSteps;
            float stepRecip = 1.0f / (float)(steps + 1);

            for (int i = 0; i < strip.sliceCount; ++i)
            {
                Vector2 currentOffset = calculateOffset(i);


                Vector2 nextOffset = calculateOffset(i + 1);


                Vector2 delta = { nextOffset.x - currentOffset.x, nextOffset.y - currentOffset.y };


                int kMax = (i == strip.sliceCount - 1) ? 0 : steps;

                for (int k = 0; k <= kMax; ++k)
                {

                    float t = (float)k * stepRecip;
                    Vector2 interpOffset = {
                        currentOffset.x + delta.x * t,
                        currentOffset.y + delta.y * t
                    };

                    Vector2 finalPos = {
                        baseT.position.x + interpOffset.x,
                        baseT.position.y + interpOffset.y
                    };

                    drawQueue.push_back({ i, finalPos });
                }
            }


            auto submit = [&](const DrawInfo& info) {
                HIKARI::Transform2D t = baseT;
                t.pivotPx = pivotPx;
                t.position = info.finalPos;

                if (cam == CameraMode::Inherit && cfg.isBillboard) {
                    t.rotation -= camRot;
                }

                drawCmd(info.sliceIndex * strip.sliceW, 0, t);
                };

            if (strip.reverseOrder) {
                for (int i = (int)drawQueue.size() - 1; i >= 0; --i) submit(drawQueue[i]);
            } else {
                for (size_t i = 0; i < drawQueue.size(); ++i) submit(drawQueue[i]);
            }
        }

        // ---- 繧ｹ繝励Λ繧､繝茨ｼ壹ワ繝ｳ繝峨Ν謖・ｮ・----
        void DrawSpriteHandle(int textureHandle,
            const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam,
            unsigned int rgba)
        {
            if (textureHandle < 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;
            cmd.transform = t;
            cmd.width = width;
            cmd.height = height;
            // 鮟倩ｮ､ UV 蜈ｨ蝗ｾ
            cmd.uv = { 0.0f, 0.0f, 1.0f, 1.0f };

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // ---- 繧ｹ繝励Λ繧､繝茨ｼ壹ワ繝ｳ繝峨Ν謖・ｮ・----
        void DrawSpriteRectHandle(int textureHandle,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam,
            unsigned int rgba)
        {
            if (textureHandle < 0) return;

            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(textureHandle, texW, texH);
            if (texW == 0 || texH == 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;
            cmd.transform = t;
            cmd.width = dstW;
            cmd.height = dstH;

            // UV 隶｡邂・
            cmd.uv.u0 = (float)srcX / texW;
            cmd.uv.v0 = (float)srcY / texH;
            cmd.uv.u1 = (float)(srcX + srcW) / texW;
            cmd.uv.v1 = (float)(srcY + srcH) / texH;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // =========================================================
        // DeformGrid 謠冗判・・X繝上Φ繝峨Ν迚茨ｼ・
        // =========================================================
        void DrawDeformGridHandle(
            int dxHandle,
            const DeformGrid& grid,
            CameraMode cam,
            unsigned int rgba)
        {
            if (dxHandle < 0) return;
            if (grid.cols < 2 || grid.rows < 2) return;
            if (grid.positions.empty()) return;

            RenderCommand cmd;
            cmd.type = CommandType::DeformGrid;
            cmd.layer = gCurrentLayer;
            // 謗貞ｺ乗ｷｱ蠎ｦ・夂ｮ蜊募叙隨ｬ荳荳ｪ轤ｹ逧・Y 蝮先・
            cmd.sortY = grid.positions[0].y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = dxHandle;
            // DeformGrid 逧・positions 騾壼ｸｸ譏ｯ荳也阜蝮先・ｼ卦ransform 隶ｾ荳ｺ Identity
            cmd.transform = HIKARI::Transform2D();
            cmd.width = 0;
            cmd.height = 0;

            // 諡ｷ雍晉ｽ第ｼ謨ｰ謐ｮ
            cmd.gridData = grid;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // =========================================================
        // DeformGrid 謠冗判・医ユ繧ｯ繧ｹ繝√Ε蜷咲沿・・
        // =========================================================
        void DrawDeformGrid(const std::string& textureName, const DeformGrid& grid, CameraMode cam, unsigned int rgba)
        {
            int h = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (h >= 0) DrawDeformGridHandle(h, grid, cam, rgba);
        }

        // =========================================================
        // MeshQuad
        // =========================================================

        void DrawMeshQuadHandleUV_Local(
            int textureHandle,
            const HIKARI::Transform2D& t,
            const Vector2& ltLocal, const Vector2& rtLocal,
            const Vector2& lbLocal, const Vector2& rbLocal,
            float u_lt, float v_lt, float u_rt, float v_rt,
            float u_lb, float v_lb, float u_rb, float v_rb,
            CameraMode cam,
            unsigned int rgba,
            bool billboard // <--- 譁ｰ蠅槫盾謨ｰ
        )
        {
            if (textureHandle < 0) return;

            // [譬ｸ蠢・ｿｮ謾ｹ] 螟・炊蟷ｿ蜻顔煙騾ｻ霎・
            HIKARI::Transform2D tt = t;
            if (billboard && cam == CameraMode::Inherit) {
                // 1. 謚ｵ豸域槍蜒乗惻譌玖ｽｬ・悟ｧ狗ｻ磯擇蜷大ｱ丞ｹ・
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                // 2. 菫ｮ豁｣ 2.5D 騾剰ｧ・ｸ狗噪 Y 霓ｴ蜴区堰
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::MeshQuad;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;

            // 蟆・ｿｮ豁｣蜷守噪 Transform 蟄伜・謖・ｻ､
            cmd.transform = tt;

            // 蟄伜・4荳ｪ螻驛ｨ鬘ｶ轤ｹ (逶ｸ蟇ｹ莠・Spine 蜴溽せ逧・攝譬・
            cmd.p0 = ltLocal; cmd.p1 = rtLocal; cmd.p2 = lbLocal; cmd.p3 = rbLocal;

            // 蟄伜・4扈ФV
            cmd.u_lt = u_lt; cmd.v_lt = v_lt;
            cmd.u_rt = u_rt; cmd.v_rt = v_rt;
            cmd.u_lb = u_lb; cmd.v_lb = v_lb;
            cmd.u_rb = u_rb; cmd.v_rb = v_rb;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            // 霑咎㈹逧・width/height 莉・畑莠・pivot 隶｡邂暦ｼ勲esh 讓｡蠑丈ｸ矩壼ｸｸ荳埼㍾隕・ｼ瑚ｮｾ荳ｺ 1 蜊ｳ蜿ｯ
            cmd.width = 1.0f;
            cmd.height = 1.0f;

            gRenderList.push_back(cmd);
        }


        // ---- SPINE ----

        void DrawSpriteRectHandleVertices(
            int textureHandle,
            int srcX, int srcY, int srcW, int srcH,
            const Vector2& ltIn, const Vector2& rtIn,
            const Vector2& lbIn, const Vector2& rbIn,
            CameraMode cam,
            unsigned int rgba
        ) {
            if (textureHandle < 0) return;
            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(textureHandle, texW, texH);
            if (texW == 0 || texH == 0) return;

            float u0 = (float)srcX / texW;
            float v0 = (float)srcY / texH;
            float u1 = (float)(srcX + srcW) / texW;
            float v1 = (float)(srcY + srcH) / texH;

            DrawMeshQuadHandleUV_Vertices(
                textureHandle,
                ltIn, rtIn, lbIn, rbIn,
                u0, v0, u1, v0,
                u0, v1, u1, v1,
                cam, rgba
            );
        }



        void DrawMeshQuadHandleUV_Vertices(
            int textureHandle,
            const Vector2& lt, const Vector2& rt,
            const Vector2& lb, const Vector2& rb,
            float u_lt, float v_lt, float u_rt, float v_rt,
            float u_lb, float v_lb, float u_rb, float v_rb,
            CameraMode cam,
            unsigned int rgba)
        {
            if (textureHandle < 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::MeshQuad;
            cmd.layer = gCurrentLayer;
            // 謗貞ｺ乗ｷｱ蠎ｦ・壼叙蠎暮Κ荳､轤ｹ逧・怙螟ｧ Y (譛髱霑醍嶌譛ｺ逧・せ)
            cmd.sortY = (lb.y > rb.y) ? lb.y : rb.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;
            // 鬘ｶ轤ｹ蟾ｲ扈乗弍荳也阜蝮先・ｺ・ｼ御ｸ埼怙隕∝・蜿俶困・卦ransform 隶ｾ荳ｺ Identity
            cmd.transform = HIKARI::Transform2D();

            // 蟄伜・4荳ｪ鬘ｶ轤ｹ
            cmd.p0 = lt; cmd.p1 = rt; cmd.p2 = lb; cmd.p3 = rb;

            // 蟄伜・4扈ФV
            cmd.u_lt = u_lt; cmd.v_lt = v_lt;
            cmd.u_rt = u_rt; cmd.v_rt = v_rt;
            cmd.u_lb = u_lb; cmd.v_lb = v_lb;
            cmd.u_rb = u_rb; cmd.v_rb = v_rb;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            cmd.width = 0; cmd.height = 0;

            gRenderList.push_back(cmd);
        }



        // ---- ANIMATION ----
        void DrawSpriteFrame(
            const std::string& textureName,
            const SpriteSheetInfo& sheet,
            int frameIndex,
            const HIKARI::Transform2D& t,
            CameraMode cam,
            unsigned int rgba)
        {
            if (sheet.frameWidth <= 0 || sheet.frameHeight <= 0 || sheet.columns <= 0) {
                return;
            }

            if (frameIndex < 0) {
                return;
            }

            int col = frameIndex % sheet.columns;
            int row = frameIndex / sheet.columns;

            int srcX = col * sheet.frameWidth;
            int srcY = row * sheet.frameHeight;
            int srcW = sheet.frameWidth;
            int srcH = sheet.frameHeight;

            DrawSpriteRect(
                textureName,
                srcX, srcY, srcW, srcH,
                t,
                static_cast<float>(srcW),
                static_cast<float>(srcH),
                cam,
                rgba
            );
        }

        // 繝輔Ξ繝ｼ繝 + MeshQuad 螟牙ｽ｢迚・
        void DrawSpriteFrameEx(
            const std::string& textureName,
            const SpriteSheetInfo& sheet,
            int frameIndex,
            const HIKARI::Transform2D& t,
            const SpriteFrameDeform& deform,
            CameraMode cam,
            unsigned int rgba)
        {
            if (sheet.frameWidth <= 0 || sheet.frameHeight <= 0 || sheet.columns <= 0) {
                return;
            }
            if (frameIndex < 0) {
                return;
            }

            // 1) 縺ｾ縺壹・騾壼ｸｸ縺ｮ繝輔Ξ繝ｼ繝險育ｮ暦ｼ医←縺ｮ繧ｳ繝槭ｒ菴ｿ縺・°・・
            int col = frameIndex % sheet.columns;
            int row = frameIndex / sheet.columns;

            int srcX = col * sheet.frameWidth;
            int srcY = row * sheet.frameHeight;
            int srcW = sheet.frameWidth;
            int srcH = sheet.frameHeight;

            // 2) DX 繝・け繧ｹ繝√Ε繝上Φ繝峨Ν・・し繧､繧ｺ蜿門ｾ・
            int dxHandle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (dxHandle < 0) {
                return;
            }

            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(dxHandle, texW, texH);
            if (texW == 0 || texH == 0) {
                return;
            }

            // 3) 縺薙・繝輔Ξ繝ｼ繝縺後ユ繧ｯ繧ｹ繝√Ε蜈ｨ菴薙・縺ｩ縺ｮ遽・峇縺具ｼ医・繝ｼ繧ｹ UV・・
            float baseU0 = static_cast<float>(srcX) / static_cast<float>(texW);
            float baseV0 = static_cast<float>(srcY) / static_cast<float>(texH);
            float baseU1 = static_cast<float>(srcX + srcW) / static_cast<float>(texW);
            float baseV1 = static_cast<float>(srcY + srcH) / static_cast<float>(texH);

            // 4) deform.uv 縺ｯ縲後ヵ繝ｬ繝ｼ繝蜀・阪〒縺ｮ繝ｭ繝ｼ繧ｫ繝ｫ UV・・・・・峨→縺励※隗｣驥・
            //    萓・ uv={0,0,1,1} 竊・繝輔Ξ繝ｼ繝蜈ｨ菴・
            auto lerp = [](float a, float b, float t) {
                return a + (b - a) * t;
                };

            float u0 = lerp(baseU0, baseU1, deform.uv.u0);
            float v0 = lerp(baseV0, baseV1, deform.uv.v0);
            float u1 = lerp(baseU0, baseU1, deform.uv.u1);
            float v1 = lerp(baseV0, baseV1, deform.uv.v1);

            // 5) 繝ｭ繝ｼ繧ｫ繝ｫ鬆らせ・医ヵ繝ｬ繝ｼ繝繧ｵ繧､繧ｺ繧貞渕貅悶↓縺励◆遏ｩ蠖｢・会ｼ九が繝輔そ繝・ヨ
            float w = static_cast<float>(srcW);
            float h = static_cast<float>(srcH);

            Vector2 ltLocal{ 0.0f, 0.0f };
            Vector2 rtLocal{ w,    0.0f };
            Vector2 lbLocal{ 0.0f, h };
            Vector2 rbLocal{ w,    h };

            ltLocal.x += deform.offsetLT.x;
            ltLocal.y += deform.offsetLT.y;
            rtLocal.x += deform.offsetRT.x;
            rtLocal.y += deform.offsetRT.y;
            lbLocal.x += deform.offsetLB.x;
            lbLocal.y += deform.offsetLB.y;
            rbLocal.x += deform.offsetRB.x;
            rbLocal.y += deform.offsetRB.y;

            // 6) Transform2D + Camera 繧剃ｽｿ縺｣縺ｦ MeshQuad 謠冗判
            unsigned int c = (rgba == 0xFFFFFFFF ? GetDefaultColor() : rgba);

            DrawMeshQuadHandleUV_Local(
                dxHandle,
                t,
                ltLocal, rtLocal, lbLocal, rbLocal,
                u0, v0,   // 蟾ｦ荳・
                u1, v0,   // 蜿ｳ荳・
                u0, v1,   // 蟾ｦ荳・
                u1, v1,   // 蜿ｳ荳・
                cam,
                c
            );
        }


    } // namespace RENDERER
} // namespace HIKARI
