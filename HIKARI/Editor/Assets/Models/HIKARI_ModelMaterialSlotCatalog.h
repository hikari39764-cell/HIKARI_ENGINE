#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Authoring/HIKARI_IModelMaterialSlotProvider.h"

namespace HIKARI::EDITOR {

// Editor 所有の on-demand catalog。AssetDatabase revision が変化した時だけ
// HMODEL を再読込し、毎フレームの Inspector からファイル I/O を除外する。
class ModelMaterialSlotCatalog final : public IModelMaterialSlotProvider {
public:
    std::span<const ModelMaterialSlotAuthoringView> ResolveMaterialSlots(const AssetDatabase& database,
                                                                         const AssetGuid& modelGuid,
                                                                         std::string& outDiagnostic) override;

    std::span<const ModelMaterialSetAuthoringView> ResolveCompatibleMaterialSets(const AssetDatabase& database,
                                                                                 const AssetGuid& modelGuid,
                                                                                 std::string& outDiagnostic) override;

    void Clear();

private:
    struct Entry {
        std::vector<ModelMaterialSlotAuthoringView> slots{};
        std::vector<ModelMaterialSetAuthoringView> materialSets{};
        std::string diagnostic{};
        std::string materialSetDiagnostic{};
    };

    void SynchronizeRevision(const AssetDatabase& database);
    Entry BuildEntry(const AssetDatabase& database, const AssetGuid& modelGuid) const;

    const AssetDatabase* database_ = nullptr;
    uint64_t contentRevision_ = 0u;
    std::unordered_map<std::string, Entry> entries_{};
};

} // namespace HIKARI::EDITOR
