#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace HIKARI {
class AssetDatabase;
class DocumentSceneBase;
} // namespace HIKARI

namespace HIKARI::EDITOR {

class EditorOperationRegistry;

struct FarModelBakeOutcome {
    bool ok = false;
    std::string farModelGuid{};
    std::string message{};
};

// 模型の元ファイルから遠景模型を焼いて Assets/Models/<名前>_far/ へ書き、導入を予約する。導入が終わるまでは元の模型が全部を描く。
FarModelBakeOutcome BakeFarModelAsset(AssetDatabase& database, std::string_view modelAssetGuid, uint32_t meshIndex);

void RegisterFarModelBakeOperation(EditorOperationRegistry& registry, std::function<DocumentSceneBase*()> scene);

} // namespace HIKARI::EDITOR
