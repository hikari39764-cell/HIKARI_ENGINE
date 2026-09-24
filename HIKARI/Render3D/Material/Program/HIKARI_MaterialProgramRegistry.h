#pragma once

#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Render3D/Material/Program/HIKARI_MaterialProgram.h"

namespace HIKARI::RENDER3D::MATERIAL {

enum class MaterialProgramResolutionKind : uint8_t {
    Compiled,
    Reused,
    StandardPbrFallback,
};

struct MaterialProgramResolution {
    MaterialProgramId id{};
    MaterialProgramResolutionKind kind = MaterialProgramResolutionKind::StandardPbrFallback;
    MaterialProgramIR effectiveProgram{};
    std::vector<MaterialCompileDiagnostic> diagnostics{};

    bool UsedFallback() const noexcept { return kind == MaterialProgramResolutionKind::StandardPbrFallback; }
};

struct MaterialProgramRegistryStats {
    uint32_t programCount = 0u;
    uint32_t sourceCount = 0u;
    uint32_t compileResolveCount = 0u;
    uint32_t compiledProgramCount = 0u;
    uint32_t reusedProgramCount = 0u;
    uint32_t fallbackResolveCount = 0u;
    uint32_t keyCollisionCount = 0u;
};

// CPU-side, single-writer registry. Editor/background compilation may
// produce MaterialCompileResult off-thread, but publication happens on the
// render/runtime owner before a frame consumes the selected program.
class MaterialProgramRegistry final {
public:
    MaterialProgramRegistry();

    void Reset();

    MaterialProgramResolution ResolveMaterialAsset(std::string_view sourceIdentity, const MaterialAssetData& source);

    MaterialProgramResolution ResolveCompileResult(std::string_view sourceIdentity,
                                                   const MaterialCompileResult& compileResult);

    const RuntimeMaterialProgram* Find(MaterialProgramId id) const noexcept;
    const RuntimeMaterialProgram& GetStandardPbrFallback() const noexcept;
    std::span<const MaterialCompileDiagnostic> GetDiagnostics(std::string_view sourceIdentity) const noexcept;
    const MaterialProgramRegistryStats& GetStats() const noexcept;

private:
    struct InternResult {
        MaterialProgramId id{};
        bool created = false;
        bool keyCollision = false;
    };

    InternResult Intern(const MaterialProgramIR& program);
    MaterialProgramResolution ResolveFailure(std::string_view sourceIdentity,
                                             std::vector<MaterialCompileDiagnostic> diagnostics);
    void StoreDiagnostics(std::string_view sourceIdentity, const std::vector<MaterialCompileDiagnostic>& diagnostics);
    void RefreshStats();

    std::deque<RuntimeMaterialProgram> programs_{};
    std::unordered_map<MaterialProgramKey, MaterialProgramId, MaterialProgramKeyHasher> programByKey_{};
    std::unordered_map<std::string, std::vector<MaterialCompileDiagnostic>> diagnosticsBySource_{};
    MaterialProgramRegistryStats stats_{};
};

std::string_view ToString(MaterialProgramResolutionKind kind) noexcept;

// Process-wide runtime publication owner. Material compilation can happen
// elsewhere, but scene/runtime publication remains single-writer.
MaterialProgramRegistry& GetRuntimeMaterialProgramRegistry();
void ResetRuntimeMaterialProgramRegistry();

} // namespace HIKARI::RENDER3D::MATERIAL
