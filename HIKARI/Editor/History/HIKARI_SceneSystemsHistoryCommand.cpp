#include "Editor/History/HIKARI_SceneSystemsHistoryCommand.h"

#include <utility>

namespace HIKARI::EDITOR {
    namespace {

        class SceneSystemsHistoryCommand final :
            public IEditorDocumentCommand {
        public:
            SceneSystemsHistoryCommand(
                std::string label,
                std::vector<SceneSystemData> before,
                std::vector<SceneSystemData> after)
                : label_(std::move(label)),
                  before_(std::move(before)),
                  after_(std::move(after)) {
            }

            const std::string& GetLabel() const noexcept override {
                return label_;
            }

            EditorDocumentImpact GetImpact() const noexcept override {
                return EditorDocumentImpact::Systems;
            }

            void Undo(SceneDocument& document) const override {
                document.systems = before_;
            }

            void Redo(SceneDocument& document) const override {
                document.systems = after_;
            }

            bool TryMergeApplied(
                const IEditorDocumentCommand&) override {
                return false;
            }

        private:
            std::string label_{};
            std::vector<SceneSystemData> before_{};
            std::vector<SceneSystemData> after_{};
        };
    }

    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneSystemsHistoryCommand(
            std::string label,
            std::vector<SceneSystemData> before,
            std::vector<SceneSystemData> after) {

        return std::make_unique<SceneSystemsHistoryCommand>(
            std::move(label),
            std::move(before),
            std::move(after));
    }

} // namespace HIKARI::EDITOR
