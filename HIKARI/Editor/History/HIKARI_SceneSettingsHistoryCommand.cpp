#include "Editor/History/HIKARI_SceneSettingsHistoryCommand.h"

#include <type_traits>
#include <utility>

namespace HIKARI::EDITOR {

namespace {

// シーン文書の一つの設定（環境の参照、焼き込みの質など）を前後の値で入れ替える。同じ名前の連続した編集は一つにまとめる。
template <auto Member, EditorDocumentImpact Impact>
class SceneSettingsHistoryCommand final : public IEditorDocumentCommand {
public:
    using Value = std::remove_cvref_t<decltype(std::declval<SceneDocument&>().*Member)>;

    SceneSettingsHistoryCommand(std::string label, Value before, Value after)
        : label_(std::move(label)), before_(std::move(before)), after_(std::move(after)) {}

    const std::string& GetLabel() const noexcept override { return label_; }

    EditorDocumentImpact GetImpact() const noexcept override { return Impact; }

    void Undo(SceneDocument& document) const override { document.*Member = before_; }

    void Redo(SceneDocument& document) const override { document.*Member = after_; }

    bool TryMergeApplied(const IEditorDocumentCommand& newer) override {
        const auto* same = dynamic_cast<const SceneSettingsHistoryCommand*>(&newer);
        if (same == nullptr || same->label_ != label_) {
            return false;
        }
        after_ = same->after_;
        return true;
    }

private:
    std::string label_{};
    Value before_{};
    Value after_{};
};

} // namespace

std::unique_ptr<IEditorDocumentCommand> MakeEnvironmentHistoryCommand(std::string label,
                                                                      ENVIRONMENT::SceneEnvironmentBinding before,
                                                                      ENVIRONMENT::SceneEnvironmentBinding after) {
    return std::make_unique<
        SceneSettingsHistoryCommand<&SceneDocument::environment, EditorDocumentImpact::Environment>>(
        std::move(label), std::move(before), std::move(after));
}

std::unique_ptr<IEditorDocumentCommand> MakeLightingBakeSettingsHistoryCommand(std::string label,
                                                                               SceneLightingBakeSettings before,
                                                                               SceneLightingBakeSettings after) {
    return std::make_unique<SceneSettingsHistoryCommand<&SceneDocument::lightingBake, EditorDocumentImpact::None>>(
        std::move(label), before, after);
}

std::unique_ptr<IEditorDocumentCommand> MakePartitionSettingsHistoryCommand(std::string label,
                                                                            ScenePartitionSettings before,
                                                                            ScenePartitionSettings after) {
    return std::make_unique<SceneSettingsHistoryCommand<&SceneDocument::partition, EditorDocumentImpact::None>>(
        std::move(label), before, after);
}

} // namespace HIKARI::EDITOR
