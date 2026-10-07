#pragma once

#include "HIKARI_AssetDatabase.h"
#include "HIKARI_AssetRegistry.h"

namespace HIKARI {

namespace CONTENT {
class EngineContentContext;
}

// ランタイム・依存検査・配布検査は同じ catalog と HTEX の登録契約を使う。
bool RegisterEngineContentTextures(const CONTENT::EngineContentContext& content, AssetRegistry& registry);

class AssetRegistryBuilder {
public:
    bool RebuildRegistry(const AssetDatabase& assetDatabase, AssetRegistry& liveRegistry) const;
};

} // namespace HIKARI
