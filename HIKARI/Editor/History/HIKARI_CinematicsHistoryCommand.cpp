#include "Editor/History/HIKARI_CinematicsHistoryCommand.h"

#include <utility>

namespace HIKARI::EDITOR {

    namespace {
        class CinematicsHistoryCommand final :
            public IEditorDocumentCommand {
        public:
            CinematicsHistoryCommand(
                std::string label,
                SceneCinematicsSettings before,
                SceneCinematicsSettings after)
                : label_(std::move(label)),
                  before_(std::move(before)),
                  after_(std::move(after)) {
            }

            const std::string& GetLabel() const noexcept override {
                return label_;
            }

            EditorDocumentImpact GetImpact() const noexcept override {
                return EditorDocumentImpact::Cinematics;
            }

            void Undo(SceneDocument& document) const override {
                document.cinematics = before_;
                NormalizeSceneCinematicsSettings(document.cinematics);
            }

            void Redo(SceneDocument& document) const override {
                document.cinematics = after_;
                NormalizeSceneCinematicsSettings(document.cinematics);
            }

            bool TryMergeApplied(
                const IEditorDocumentCommand& newer) override {

                const auto* cinematics =
                    dynamic_cast<const CinematicsHistoryCommand*>(&newer);
                if (cinematics == nullptr) {
                    return false;
                }
                after_ = cinematics->after_;
                return true;
            }

        private:
            std::string label_{};
            SceneCinematicsSettings before_{};
            SceneCinematicsSettings after_{};
        };
    }

    std::unique_ptr<IEditorDocumentCommand> MakeCinematicsHistoryCommand(
        std::string label,
        SceneCinematicsSettings before,
        SceneCinematicsSettings after) {

        return std::make_unique<CinematicsHistoryCommand>(
            std::move(label),
            std::move(before),
            std::move(after));
    }

} // namespace HIKARI::EDITOR
