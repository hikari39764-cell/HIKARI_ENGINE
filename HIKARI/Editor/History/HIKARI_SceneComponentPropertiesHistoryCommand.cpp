#include "Editor/History/HIKARI_SceneComponentPropertiesHistoryCommand.h"

#include <utility>

#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::EDITOR {

    namespace {

        class SceneComponentPropertiesHistoryCommand final :
            public IEditorDocumentCommand {
        public:
            SceneComponentPropertiesHistoryCommand(
                std::string label,
                SceneObjectId objectId,
                SceneComponentId componentId,
                nlohmann::json beforeProperties,
                nlohmann::json afterProperties,
                EditorDocumentImpact impact)
                : label_(std::move(label)),
                  objectId_(objectId),
                  componentId_(componentId),
                  beforeProperties_(std::move(beforeProperties)),
                  afterProperties_(std::move(afterProperties)),
                  impact_(impact) {
            }

            const std::string& GetLabel() const noexcept override {
                return label_;
            }

            EditorDocumentImpact GetImpact() const noexcept override {
                return impact_;
            }

            void Undo(SceneDocument& document) const override {
                Apply(document, beforeProperties_);
            }

            void Redo(SceneDocument& document) const override {
                Apply(document, afterProperties_);
            }

            bool TryMergeApplied(
                const IEditorDocumentCommand& newer) override {

                const auto* properties = dynamic_cast<
                    const SceneComponentPropertiesHistoryCommand*>(&newer);
                if (properties == nullptr ||
                    properties->label_ != label_ ||
                    properties->objectId_ != objectId_ ||
                    properties->componentId_ != componentId_ ||
                    properties->impact_ != impact_) {
                    return false;
                }
                afterProperties_ = properties->afterProperties_;
                return true;
            }

        private:
            void Apply(
                SceneDocument& document,
                const nlohmann::json& properties) const {

                SceneObjectData* object =
                    FindSceneObjectData(document, objectId_);
                if (object == nullptr) {
                    return;
                }
                SceneComponentData* component =
                    FindSceneComponent(*object, componentId_);
                if (component == nullptr) {
                    return;
                }
                component->properties = properties;
            }

            std::string label_{};
            SceneObjectId objectId_{};
            SceneComponentId componentId_{};
            nlohmann::json beforeProperties_ = nlohmann::json::object();
            nlohmann::json afterProperties_ = nlohmann::json::object();
            EditorDocumentImpact impact_ = EditorDocumentImpact::RuntimeWorld;
        };

        class SceneComponentPropertiesPatchHistoryCommand final :
            public IEditorDocumentCommand {
        public:
            SceneComponentPropertiesPatchHistoryCommand(
                std::string label,
                SceneObjectId objectId,
                SceneComponentId componentId,
                nlohmann::json beforePatch,
                nlohmann::json afterPatch,
                EditorDocumentImpact impact)
                : label_(std::move(label)),
                  objectId_(objectId),
                  componentId_(componentId),
                  beforePatch_(std::move(beforePatch)),
                  afterPatch_(std::move(afterPatch)),
                  impact_(impact) {
            }

            const std::string& GetLabel() const noexcept override {
                return label_;
            }

            EditorDocumentImpact GetImpact() const noexcept override {
                return impact_;
            }

            void Undo(SceneDocument& document) const override {
                Apply(document, beforePatch_);
            }

            void Redo(SceneDocument& document) const override {
                Apply(document, afterPatch_);
            }

            bool TryMergeApplied(
                const IEditorDocumentCommand& newer) override {
                const auto* patch = dynamic_cast<const
                    SceneComponentPropertiesPatchHistoryCommand*>(&newer);
                if (patch == nullptr ||
                    patch->label_ != label_ ||
                    patch->objectId_ != objectId_ ||
                    patch->componentId_ != componentId_ ||
                    patch->impact_ != impact_) {
                    return false;
                }
                afterPatch_ = patch->afterPatch_;
                return true;
            }

        private:
            void Apply(
                SceneDocument& document,
                const nlohmann::json& patch) const {
                if (!patch.is_object()) {
                    return;
                }
                SceneObjectData* object =
                    FindSceneObjectData(document, objectId_);
                if (object == nullptr) {
                    return;
                }
                SceneComponentData* component =
                    FindSceneComponent(*object, componentId_);
                if (component == nullptr) {
                    return;
                }
                if (!component->properties.is_object()) {
                    component->properties = nlohmann::json::object();
                }
                for (auto it = patch.begin(); it != patch.end(); ++it) {
                    component->properties[it.key()] = it.value();
                }
            }

            std::string label_{};
            SceneObjectId objectId_{};
            SceneComponentId componentId_{};
            nlohmann::json beforePatch_ = nlohmann::json::object();
            nlohmann::json afterPatch_ = nlohmann::json::object();
            EditorDocumentImpact impact_ = EditorDocumentImpact::RuntimeWorld;
        };

        class SceneTerrainSurfaceEditDeltaHistoryCommand final :
            public IEditorDocumentCommand {
        public:
            SceneTerrainSurfaceEditDeltaHistoryCommand(
                std::string label,
                SceneObjectId objectId,
                SceneComponentId componentId,
                nlohmann::json beforeDelta,
                nlohmann::json afterDelta,
                EditorDocumentImpact impact)
                : label_(std::move(label)),
                  objectId_(objectId),
                  componentId_(componentId),
                  beforeDelta_(std::move(beforeDelta)),
                  afterDelta_(std::move(afterDelta)),
                  impact_(impact) {
            }

            const std::string& GetLabel() const noexcept override {
                return label_;
            }

            EditorDocumentImpact GetImpact() const noexcept override {
                return impact_;
            }

            void Undo(SceneDocument& document) const override {
                Apply(document, beforeDelta_);
            }

            void Redo(SceneDocument& document) const override {
                Apply(document, afterDelta_);
            }

            bool TryMergeApplied(
                const IEditorDocumentCommand&) override {
                // Each drag is one explicit authoring transaction. Never
                // merge two strokes across a pointer release boundary.
                return false;
            }

        private:
            void Apply(
                SceneDocument& document,
                const nlohmann::json& delta) const {
                SceneObjectData* object =
                    FindSceneObjectData(document, objectId_);
                if (object == nullptr) {
                    return;
                }
                SceneComponentData* component =
                    FindSceneComponent(*object, componentId_);
                if (component == nullptr) {
                    return;
                }
                (void)ApplyTerrainSurfaceEditDelta(
                    component->properties, delta);
            }

            std::string label_{};
            SceneObjectId objectId_{};
            SceneComponentId componentId_{};
            nlohmann::json beforeDelta_ = nlohmann::json::object();
            nlohmann::json afterDelta_ = nlohmann::json::object();
            EditorDocumentImpact impact_ = EditorDocumentImpact::RuntimeWorld;
        };

    } // namespace

    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneComponentPropertiesHistoryCommand(
            std::string label,
            SceneObjectId objectId,
            SceneComponentId componentId,
            nlohmann::json beforeProperties,
            nlohmann::json afterProperties,
            EditorDocumentImpact impact) {

        return std::make_unique<SceneComponentPropertiesHistoryCommand>(
            std::move(label),
            objectId,
            componentId,
            std::move(beforeProperties),
            std::move(afterProperties),
            impact);
    }

    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneComponentPropertiesPatchHistoryCommand(
            std::string label,
            SceneObjectId objectId,
            SceneComponentId componentId,
            nlohmann::json beforePatch,
            nlohmann::json afterPatch,
            EditorDocumentImpact impact) {

        return std::make_unique<
            SceneComponentPropertiesPatchHistoryCommand>(
                std::move(label),
                objectId,
                componentId,
                std::move(beforePatch),
                std::move(afterPatch),
                impact);
    }

    std::unique_ptr<IEditorDocumentCommand>
        MakeSceneTerrainSurfaceEditDeltaHistoryCommand(
            std::string label,
            SceneObjectId objectId,
            SceneComponentId componentId,
            nlohmann::json beforeDelta,
            nlohmann::json afterDelta,
            EditorDocumentImpact impact) {
        return std::make_unique<
            SceneTerrainSurfaceEditDeltaHistoryCommand>(
                std::move(label),
                objectId,
                componentId,
                std::move(beforeDelta),
                std::move(afterDelta),
                impact);
    }

} // namespace HIKARI::EDITOR
