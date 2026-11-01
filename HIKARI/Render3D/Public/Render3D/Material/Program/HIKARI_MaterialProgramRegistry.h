#pragma once

#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Render3D/Material/Program/HIKARI_MaterialProgram.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"

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

// 標準 PBR の固定経路（program 0）が材質コンパイラ（Assets）から受け取るもの。コンパイラは上の層にあるので、
// registry の持ち主が組んで渡す。
struct StandardPbrProgramSource {
    // コンパイルに失敗した材質が使う回退プログラム。registry の slot 0 に入る。
    MaterialProgramIR fallback{};
    // 標準 PBR の形の資産データを一つコンパイルする。固定経路へ寄せてよい材質かの判定と、パラメーターだけで組む
    // 材質のプログラム作りが使う。
    MaterialCompileResult (*compile)(const MaterialAssetData&) = nullptr;
};

// CPU-side, single-writer registry. Editor/background compilation may
// produce MaterialCompileResult off-thread, but publication happens on the
// render/runtime owner before a frame consumes the selected program.
class MaterialProgramRegistry final {
public:
    // source を渡すまで slot 0 は空のプログラムで、標準 PBR の固定経路へ寄せる判定とコンパイルは働かない。
    explicit MaterialProgramRegistry(StandardPbrProgramSource source = {});

    void Reset(StandardPbrProgramSource source);
    // 標準 PBR の形の資産データをコンパイルする。source に compile が無ければ失敗の結果を返す。
    MaterialCompileResult CompileStandardForm(const MaterialAssetData& data) const;

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
    MaterialCompileResult (*compileStandardForm_)(const MaterialAssetData&) = nullptr;
};

std::string_view ToString(MaterialProgramResolutionKind kind) noexcept;

// Process-wide runtime publication owner. Material compilation can happen
// elsewhere, but scene/runtime publication remains single-writer.
MaterialProgramRegistry& GetRuntimeMaterialProgramRegistry();
// 生成した Ramp 貼図を手放し、registry を source で作り直す。起動で標準 PBR の出どころを渡し、終了で空に戻す。
void ResetRuntimeMaterialProgramRegistry(StandardPbrProgramSource source = {});

} // namespace HIKARI::RENDER3D::MATERIAL
