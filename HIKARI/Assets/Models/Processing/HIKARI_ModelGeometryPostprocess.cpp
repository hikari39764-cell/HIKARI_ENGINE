#include "Assets/Models/Processing/HIKARI_ModelGeometryPostprocess.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_TangentFrame.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"

namespace HIKARI::ASSETS::MODELS {

void GenerateModelPrimitiveNormals(MeshPrimitive& primitive) {
    for (Vertex3D& vertex : primitive.staticVertices) {
        vertex.normal = {};
    }

    for (size_t i = 0; i + 2u < primitive.indices.size(); i += 3u) {
        const uint32_t i0 = primitive.indices[i + 0u];
        const uint32_t i1 = primitive.indices[i + 1u];
        const uint32_t i2 = primitive.indices[i + 2u];
        if (i0 >= primitive.staticVertices.size() || i1 >= primitive.staticVertices.size() ||
            i2 >= primitive.staticVertices.size()) {
            continue;
        }

        const MATH::Vec3 p0 = primitive.staticVertices[i0].position;
        const MATH::Vec3 p1 = primitive.staticVertices[i1].position;
        const MATH::Vec3 p2 = primitive.staticVertices[i2].position;
        const MATH::Vec3 normal = MATH::Normalize(MATH::Cross(p1 - p0, p2 - p0));
        if (MATH::Length(normal) <= 1.0e-6f) {
            continue;
        }

        primitive.staticVertices[i0].normal = primitive.staticVertices[i0].normal + normal;
        primitive.staticVertices[i1].normal = primitive.staticVertices[i1].normal + normal;
        primitive.staticVertices[i2].normal = primitive.staticVertices[i2].normal + normal;
    }

    for (Vertex3D& vertex : primitive.staticVertices) {
        vertex.normal = MATH::Normalize(vertex.normal);
        if (MATH::Length(vertex.normal) <= 1.0e-6f) {
            vertex.normal = {0.0f, 1.0f, 0.0f};
        }
    }
}

void GenerateModelPrimitiveTangents(MeshPrimitive& primitive) {
    std::vector<MATH::Vec3> tangentAccum(primitive.staticVertices.size());
    std::vector<MATH::Vec3> bitangentAccum(primitive.staticVertices.size());

    for (size_t i = 0; i + 2u < primitive.indices.size(); i += 3u) {
        const uint32_t i0 = primitive.indices[i + 0u];
        const uint32_t i1 = primitive.indices[i + 1u];
        const uint32_t i2 = primitive.indices[i + 2u];
        if (i0 >= primitive.staticVertices.size() || i1 >= primitive.staticVertices.size() ||
            i2 >= primitive.staticVertices.size()) {
            continue;
        }

        const Vertex3D& v0 = primitive.staticVertices[i0];
        const Vertex3D& v1 = primitive.staticVertices[i1];
        const Vertex3D& v2 = primitive.staticVertices[i2];
        const MATH::Vec3 edge1 = v1.position - v0.position;
        const MATH::Vec3 edge2 = v2.position - v0.position;
        const MATH::Vec2 duv1 = v1.uv0 - v0.uv0;
        const MATH::Vec2 duv2 = v2.uv0 - v0.uv0;

        const float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
        if (std::abs(determinant) <= 1.0e-8f) {
            continue;
        }

        const float inverse = 1.0f / determinant;
        const MATH::Vec3 tangent = (edge1 * duv2.y - edge2 * duv1.y) * inverse;
        const MATH::Vec3 bitangent = (edge2 * duv1.x - edge1 * duv2.x) * inverse;

        tangentAccum[i0] = tangentAccum[i0] + tangent;
        tangentAccum[i1] = tangentAccum[i1] + tangent;
        tangentAccum[i2] = tangentAccum[i2] + tangent;
        bitangentAccum[i0] = bitangentAccum[i0] + bitangent;
        bitangentAccum[i1] = bitangentAccum[i1] + bitangent;
        bitangentAccum[i2] = bitangentAccum[i2] + bitangent;
    }

    for (size_t i = 0; i < primitive.staticVertices.size(); ++i) {
        MATH::Vec3 normal = MATH::Normalize(primitive.staticVertices[i].normal);
        if (MATH::Length(normal) <= 1.0e-6f) {
            normal = {0.0f, 1.0f, 0.0f};
        }

        MATH::Vec3 tangent = tangentAccum[i] - normal * MATH::Dot(normal, tangentAccum[i]);
        tangent = MATH::Normalize(tangent);
        if (MATH::Length(tangent) <= 1.0e-6f) {
            tangent = MATH::BuildFallbackTangent(normal);
        }
        if (MATH::Length(tangent) <= 1.0e-6f) {
            tangent = {1.0f, 0.0f, 0.0f};
        }

        const float handedness = MATH::Dot(MATH::Cross(normal, tangent), bitangentAccum[i]) < 0.0f ? -1.0f : 1.0f;
        primitive.staticVertices[i].tangent = {tangent.x, tangent.y, tangent.z, handedness};
    }
}

namespace {

bool IsNearIdentity(const MATH::Mat4& value) {
    const MATH::Mat4 identity = MATH::Mat4::Identity();
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (std::abs(value.m[column][row] - identity.m[column][row]) > 1.0e-5f)
                return false;
    return true;
}

// 一様で正の拡縮を持つ TRS に分解できるか（法線を同じ行列で回せる）。
bool DecomposeUniform(const MATH::Mat4& value, Transform3D& outTransform) {
    MATH::Vec3 scale{};
    if (!MATH::DecomposeTRS(value, outTransform.position, outTransform.rotation, scale) || scale.x <= 0.0f)
        return false;
    outTransform.scale = scale;
    const float tolerance = 1.0e-4f * scale.x;
    return std::abs(scale.y - scale.x) <= tolerance && std::abs(scale.z - scale.x) <= tolerance;
}

template <typename Vertex>
void BakeVertices(std::vector<Vertex>& vertices, const MATH::Mat4& transform) {
    for (Vertex& vertex : vertices) {
        vertex.position = MATH::TransformPoint(transform, vertex.position);
        vertex.normal = MATH::Normalize(MATH::TransformVector(transform, vertex.normal));
        const MATH::Vec3 tangent =
            MATH::Normalize(MATH::TransformVector(transform, {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z}));
        vertex.tangent = {tangent.x, tangent.y, tangent.z, vertex.tangent.w};
    }
}

} // namespace

void BakeSkinnedMeshNodes(ModelAsset& asset) {
    const size_t count = asset.nodes.size();
    // 節点の並びは形式ごとに違う（glTF は親が子より後に来ることもある）ので、親を辿って global を求める。親の番号が
    // 壊れているか輪になっていれば何もしない。
    std::vector<MATH::Mat4> globals(count, MATH::Mat4::Identity());
    std::vector<bool> resolved(count, false);
    std::vector<size_t> chain{};
    for (size_t index = 0; index < count; ++index) {
        chain.clear();
        int cursor = static_cast<int>(index);
        while (cursor >= 0) {
            if (cursor >= static_cast<int>(count) || chain.size() >= count)
                return;
            if (resolved[static_cast<size_t>(cursor)])
                break;
            chain.push_back(static_cast<size_t>(cursor));
            cursor = asset.nodes[static_cast<size_t>(cursor)].parent;
        }
        for (size_t step = chain.size(); step-- > 0u;) {
            const size_t current = chain[step];
            const int parent = asset.nodes[current].parent;
            const MATH::Mat4 local = BOUNDS::GetNodeLocalMatrix(asset.nodes[current]);
            globals[current] = parent >= 0 ? globals[static_cast<size_t>(parent)] * local : local;
            resolved[current] = true;
        }
    }
    std::vector<bool> pinned(count, false);
    std::vector<uint32_t> meshUses(asset.meshes.size(), 0u);
    for (const ModelNode& node : asset.nodes)
        if (node.meshIndex >= 0 && node.meshIndex < static_cast<int>(meshUses.size()))
            ++meshUses[static_cast<size_t>(node.meshIndex)];
    // 関節と動画の対象の節点は動くので焼かない。
    for (const SkeletonAsset& skin : asset.skins)
        for (const SkeletonJoint& joint : skin.joints)
            if (joint.nodeIndex >= 0 && joint.nodeIndex < static_cast<int>(count))
                pinned[static_cast<size_t>(joint.nodeIndex)] = true;
    for (const AnimationClip& clip : asset.animations)
        for (const NodeAnimationChannel& channel : clip.channels)
            if (channel.targetNode >= 0 && channel.targetNode < static_cast<int>(count))
                pinned[static_cast<size_t>(channel.targetNode)] = true;

    uint32_t baked = 0u;
    for (size_t index = 0; index < count; ++index) {
        ModelNode& node = asset.nodes[index];
        if (node.meshIndex < 0 || node.skinIndex < 0 || pinned[index] || IsNearIdentity(globals[index]))
            continue;
        // 節点の網格（自身と子）が全て蒙皮で、どれも一度だけ使われる時だけ焼く。子は /MeshN の分片か、FBX の蒙皮網格の
        // 子節点（自分の局所の変換を持つ）。どれも自分の global で焼き、焼いた後の global が単位になるよう局所を直す。
        std::vector<size_t> carriers{index};
        bool bakeable = true;
        for (const int child : node.children) {
            const ModelNode& slot = asset.nodes[static_cast<size_t>(child)];
            bakeable = bakeable && slot.meshIndex >= 0 && slot.skinIndex >= 0 && slot.children.empty() &&
                       !pinned[static_cast<size_t>(child)];
            carriers.push_back(static_cast<size_t>(child));
        }
        for (const size_t carrier : carriers) {
            const ModelNode& carried = asset.nodes[carrier];
            Transform3D global{};
            bakeable = bakeable && carried.meshIndex < static_cast<int>(meshUses.size()) &&
                       meshUses[static_cast<size_t>(carried.meshIndex)] == 1u &&
                       carried.skinIndex < static_cast<int>(asset.skins.size()) &&
                       DecomposeUniform(globals[carrier], global);
        }
        Transform3D local{};
        const MATH::Mat4 parentGlobal =
            node.parent >= 0 ? globals[static_cast<size_t>(node.parent)] : MATH::Mat4::Identity();
        const MATH::Mat4 localMatrix = MATH::Inverse(parentGlobal);
        if (!bakeable || !DecomposeUniform(localMatrix, local))
            continue;
        for (const size_t carrier : carriers) {
            ModelNode& carried = asset.nodes[carrier];
            const MATH::Mat4 carrierGlobal = globals[carrier];
            const MATH::Mat4 inverseGlobal = MATH::Inverse(carrierGlobal);
            MeshAsset& mesh = asset.meshes[static_cast<size_t>(carried.meshIndex)];
            for (MeshPrimitive& primitive : mesh.primitives) {
                BakeVertices(primitive.staticVertices, carrierGlobal);
                BakeVertices(primitive.skinnedVertices, carrierGlobal);
                primitive.bounds = BOUNDS::ComputePrimitiveBounds(primitive);
            }
            mesh.bounds = BOUNDS::ComputeMeshBounds(mesh);
            for (SkeletonJoint& joint : asset.skins[static_cast<size_t>(carried.skinIndex)].joints)
                joint.inverseBindMatrix = joint.inverseBindMatrix * inverseGlobal;
            if (carrier != index) {
                carried.localMatrix = MATH::Mat4::Identity();
                carried.localTransform = Transform3D{};
            }
            globals[carrier] = MATH::Mat4::Identity();
        }
        node.localMatrix = localMatrix;
        node.localTransform = local;
        ++baked;
    }
    if (baked > 0u)
        asset.importDiagnostics.messages.push_back("[Model] skinned mesh node transforms baked into vertices nodes=" +
                                                   std::to_string(baked));
}

bool ValidateModelPrimitiveStreams(const ModelAsset& model, std::string& outMessage) {
    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
        const MeshAsset& mesh = model.meshes[meshIndex];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
            const MeshPrimitive& primitive = mesh.primitives[primitiveIndex];
            const bool skinned = primitive.layout == VertexLayoutKind::SkinnedPNTTJW;
            const size_t vertexCount = skinned ? primitive.skinnedVertices.size() : primitive.staticVertices.size();
            const char* reason = nullptr;
            if (skinned && !primitive.staticVertices.empty() && primitive.staticVertices.size() != vertexCount)
                reason = "skinned and static vertex streams differ in length";
            else if (!skinned && !primitive.skinnedVertices.empty())
                reason = "static layout carries skinned vertices";
            else if (primitive.indices.size() % 3u != 0u)
                reason = "index count is not a multiple of 3";
            else if (std::any_of(primitive.indices.begin(), primitive.indices.end(),
                                 [vertexCount](uint32_t index) { return index >= vertexCount; }))
                reason = "an index references a missing vertex";
            if (reason != nullptr) {
                outMessage = "mesh=" + mesh.name + " (" + std::to_string(meshIndex) +
                             ") primitive=" + std::to_string(primitiveIndex) + ": " + reason;
                return false;
            }
        }
    }
    return true;
}

} // namespace HIKARI::ASSETS::MODELS
