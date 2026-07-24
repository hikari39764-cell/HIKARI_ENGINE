#pragma once

#include <cstdint>

#include <json.hpp>

namespace HIKARI {

    enum class ProceduralMeshKind : uint8_t {
        Plane,
        GridPlane,
        Box,
        Sphere,
        Cylinder,
        Capsule,
    };

    struct ProceduralMeshSettings {
        ProceduralMeshKind kind = ProceduralMeshKind::Box;
        float width = 1.0f;
        float height = 1.0f;
        float depth = 1.0f;
        uint32_t segmentsX = 10;
        uint32_t segmentsY = 10;
        uint32_t segmentsZ = 1;
        uint32_t sphereSlices = 32;
        uint32_t sphereStacks = 16;
        // 両面描画は cluster の cone culling を無効化するため、必要な面だけ明示的に有効化する。
        bool doubleSided = false;
        bool generateTangents = true;

        bool operator==(const ProceduralMeshSettings&) const = default;
    };

    const char* ToString(ProceduralMeshKind kind) noexcept;
    const char* GetProceduralMeshDisplayName(
        ProceduralMeshKind kind) noexcept;
    ProceduralMeshKind ParseProceduralMeshKind(
        const nlohmann::json& value,
        ProceduralMeshKind fallback = ProceduralMeshKind::Box);
    ProceduralMeshSettings SanitizeProceduralMeshSettings(
        ProceduralMeshSettings settings) noexcept;

} // namespace HIKARI
