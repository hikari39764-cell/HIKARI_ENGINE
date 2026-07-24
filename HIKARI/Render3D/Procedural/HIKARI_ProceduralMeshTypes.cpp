#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace HIKARI {

    const char* ToString(ProceduralMeshKind kind) noexcept {
        switch (kind) {
        case ProceduralMeshKind::Plane: return "Plane";
        case ProceduralMeshKind::GridPlane: return "GridPlane";
        case ProceduralMeshKind::Box: return "Box";
        case ProceduralMeshKind::Sphere: return "Sphere";
        case ProceduralMeshKind::Cylinder: return "Cylinder";
        case ProceduralMeshKind::Capsule: return "Capsule";
        default: return "Box";
        }
    }

    const char* GetProceduralMeshDisplayName(
        ProceduralMeshKind kind) noexcept {

        return kind == ProceduralMeshKind::GridPlane
            ? "Grid Plane"
            : ToString(kind);
    }

    ProceduralMeshKind ParseProceduralMeshKind(
        const nlohmann::json& value,
        ProceduralMeshKind fallback) {

        if (value.is_number_integer()) {
            const int index = value.get<int>();
            return index >= 0 && index <= 5
                ? static_cast<ProceduralMeshKind>(index)
                : fallback;
        }
        if (!value.is_string()) {
            return fallback;
        }
        const std::string text = value.get<std::string>();
        if (text == "Plane") return ProceduralMeshKind::Plane;
        if (text == "GridPlane" || text == "Grid") return ProceduralMeshKind::GridPlane;
        if (text == "Box" || text == "Cube") return ProceduralMeshKind::Box;
        if (text == "Sphere") return ProceduralMeshKind::Sphere;
        if (text == "Cylinder") return ProceduralMeshKind::Cylinder;
        if (text == "Capsule") return ProceduralMeshKind::Capsule;
        return fallback;
    }

    ProceduralMeshSettings SanitizeProceduralMeshSettings(
        ProceduralMeshSettings settings) noexcept {

        const auto finiteDimension = [](float value) {
            const float clamped = std::clamp(
                std::isfinite(value) ? value : 1.0f,
                0.001f,
                100000.0f);
            return std::round(clamped * 1000.0f) * 0.001f;
        };
        settings.width = finiteDimension(settings.width);
        settings.height = finiteDimension(settings.height);
        settings.depth = finiteDimension(settings.depth);
        settings.segmentsX = std::clamp(settings.segmentsX, 1u, 512u);
        settings.segmentsY = std::clamp(settings.segmentsY, 1u, 512u);
        settings.segmentsZ = std::clamp(settings.segmentsZ, 1u, 64u);
        settings.sphereSlices = std::clamp(settings.sphereSlices, 3u, 512u);
        settings.sphereStacks = std::clamp(settings.sphereStacks, 2u, 256u);

        switch (settings.kind) {
        case ProceduralMeshKind::Plane:
            settings.segmentsX = 1u;
            settings.segmentsY = 1u;
            break;
        case ProceduralMeshKind::Sphere:
            settings.height = settings.width;
            settings.depth = settings.width;
            break;
        case ProceduralMeshKind::Cylinder:
            settings.depth = settings.width;
            break;
        case ProceduralMeshKind::Capsule:
            settings.depth = settings.width;
            settings.height = (std::max)(settings.height, settings.width);
            break;
        default:
            break;
        }
        return settings;
    }

} // namespace HIKARI
