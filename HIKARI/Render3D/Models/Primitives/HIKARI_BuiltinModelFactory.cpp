#include "Render3D/Models/Primitives/HIKARI_BuiltinModelFactory.h"

#include <array>
#include <memory>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::MODELS {

    bool BuildBuiltinCubeModel(ModelAsset& asset) {
        auto mesh = std::make_unique<Mesh>();
        std::vector<VertexStatic3D> vertices;
        std::vector<uint32_t> indices;

        const std::array<MATH::Vec3, 8> p = {
            MATH::Vec3{-0.5f,-0.5f,-0.5f}, MATH::Vec3{0.5f,-0.5f,-0.5f}, MATH::Vec3{0.5f,0.5f,-0.5f}, MATH::Vec3{-0.5f,0.5f,-0.5f},
            MATH::Vec3{-0.5f,-0.5f, 0.5f}, MATH::Vec3{0.5f,-0.5f, 0.5f}, MATH::Vec3{0.5f,0.5f, 0.5f}, MATH::Vec3{-0.5f,0.5f, 0.5f}
        };

        auto pushTri = [&](int i0, int i1, int i2, const MATH::Vec3& n) {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            VertexStatic3D v0{};
            v0.position = p[i0];
            v0.normal = n;
            v0.u = 0.0f;
            v0.v = 0.0f;
            v0.uv1 = { v0.u, v0.v };
            VertexStatic3D v1{};
            v1.position = p[i1];
            v1.normal = n;
            v1.u = 1.0f;
            v1.v = 0.0f;
            v1.uv1 = { v1.u, v1.v };
            VertexStatic3D v2{};
            v2.position = p[i2];
            v2.normal = n;
            v2.u = 1.0f;
            v2.v = 1.0f;
            v2.uv1 = { v2.u, v2.v };
            vertices.push_back(v0);
            vertices.push_back(v1);
            vertices.push_back(v2);
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        };

        pushTri(0, 1, 2, { 0, 0, -1 }); pushTri(0, 2, 3, { 0, 0, -1 });
        pushTri(5, 4, 7, { 0, 0, 1 });  pushTri(5, 7, 6, { 0, 0, 1 });
        pushTri(4, 0, 3, { -1, 0, 0 }); pushTri(4, 3, 7, { -1, 0, 0 });
        pushTri(1, 5, 6, { 1, 0, 0 });  pushTri(1, 6, 2, { 1, 0, 0 });
        pushTri(3, 2, 6, { 0, 1, 0 });  pushTri(3, 6, 7, { 0, 1, 0 });
        pushTri(4, 5, 1, { 0, -1, 0 }); pushTri(4, 1, 0, { 0, -1, 0 });

        if (!mesh->CreateStatic(SERVICES::gCtx.device, vertices, indices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        material->SetBaseColor({ 0.85f, 0.9f, 1.0f, 1.0f });

        asset.bounds = { { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };
        asset.SetLegacyRuntimeMesh(std::move(mesh));
        asset.SetLegacyRuntimeMaterial(std::move(material));
        return true;
    }


} // namespace HIKARI::RENDER3D::MODELS
