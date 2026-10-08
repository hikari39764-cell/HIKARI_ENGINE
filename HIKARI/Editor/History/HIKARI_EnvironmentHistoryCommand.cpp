#include "Editor/History/HIKARI_EnvironmentHistoryCommand.h"

#include <utility>

namespace HIKARI::EDITOR {

namespace {

class EnvironmentHistoryCommand final : public IEditorDocumentCommand {
public:
    EnvironmentHistoryCommand(std::string label, ENVIRONMENT::SceneEnvironmentBinding before,
                              ENVIRONMENT::SceneEnvironmentBinding after)
        : label_(std::move(label)), before_(std::move(before)), after_(std::move(after)) {}

    const std::string& GetLabel() const noexcept override { return label_; }

    EditorDocumentImpact GetImpact() const noexcept override { return EditorDocumentImpact::Environment; }

    void Undo(SceneDocument& document) const override { document.environment = before_; }

    void Redo(SceneDocument& document) const override { document.environment = after_; }

    bool TryMergeApplied(const IEditorDocumentCommand& newer) override {
        const auto* environment = dynamic_cast<const EnvironmentHistoryCommand*>(&newer);
        if (environment == nullptr || environment->label_ != label_) {
            return false;
        }
        after_ = environment->after_;
        return true;
    }

private:
    std::string label_{};
    ENVIRONMENT::SceneEnvironmentBinding before_{};
    ENVIRONMENT::SceneEnvironmentBinding after_{};
};

} // namespace

std::unique_ptr<IEditorDocumentCommand> MakeEnvironmentHistoryCommand(std::string label,
                                                                      ENVIRONMENT::SceneEnvironmentBinding before,
                                                                      ENVIRONMENT::SceneEnvironmentBinding after) {
    return std::make_unique<EnvironmentHistoryCommand>(std::move(label), std::move(before), std::move(after));
}

} // namespace HIKARI::EDITOR
