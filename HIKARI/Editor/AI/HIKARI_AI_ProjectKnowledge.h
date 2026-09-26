#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {
class EditorOperationRegistry;

// プロジェクトの制作ガイド（画風・命名・よく使う資産など）。ユーザーが書き、AI は各依頼の文脈として読む。
std::filesystem::path GetProjectGuidePath(const std::filesystem::path& projectRoot);
// 文脈に入れるガイド本文。長すぎる場合は先頭だけを返し、全文は project.guide.read で読む。
std::string ReadProjectGuideForContext(const std::filesystem::path& projectRoot);

// project.guide.read / project.guide.write と、資産カタログの asset.query を登録する。
void RegisterProjectKnowledgeOperations(EditorOperationRegistry& registry, std::function<DocumentSceneBase*()> scene);

} // namespace HIKARI::EDITOR
