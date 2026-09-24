#pragma once
#include "Editor/Documents/HIKARI_UiAnimationEditorDocument.h"
#include "Editor/Widgets/HIKARI_ScalarCurveEditor.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include <functional>
#include <set>

namespace HIKARI {
class DocumentSceneBase;
struct EditorContext;
} // namespace HIKARI
namespace HIKARI::EDITOR {
class EditorOperationRegistry;
class EditorCommandRouter;
class UiAnimationEditor {
public:
    UiAnimationEditorDocument& Document() { return document_; }
    const UiAnimationEditorDocument& Document() const { return document_; }
    UiAnimationPlayer& Player() { return player_; }
    void Draw(DocumentSceneBase&, EditorContext&, EditorOperationRegistry&);
    void Tick(DocumentSceneBase&, float delta);
    bool Preview(DocumentSceneBase&, SceneObjectId, std::string_view clip, float seconds, bool play);
    void StopPreview();
    void BindCommands(EditorCommandRouter&);
    bool HasFocus() const { return focused_; }
    SceneObjectId PreviewRoot() const { return preview_ ? root_ : SceneObjectId{}; }
    bool IsPreviewing() const { return preview_; }
    bool Open(AssetDatabase&, const AssetGuid&);
    bool TravelHistory(bool redo);

private:
    void DrawTimeline(UiAnimationAsset&, UiAnimationClip&, bool& changed);
    void BindToSelection(DocumentSceneBase&, EditorContext&, EditorOperationRegistry&);
    bool AssignTarget(DocumentSceneBase&, EditorContext&, EditorOperationRegistry&, UiAnimationTrack&);
    UiAnimationEditorDocument document_;
    UiAnimationPlayer player_;
    SceneObjectId root_{};
    uint64_t previewWorld_ = 0;
    RuntimeObjectHandle previewHandle_{};
    uint64_t clip_ = 0, track_ = 0, previewRevision_ = ~uint64_t(0);
    float time_ = 0;
    bool preview_ = false, playing_ = false, focused_ = false, autoKey_ = false, loop_ = false;
    bool curve_ = false, snap_ = true;
    ScalarCurveView curveView_;
    std::string message_;
    char search_[128]{};
    std::set<std::pair<uint64_t, size_t>> selectedKeys_;
    struct CopiedKey {
        std::string slot;
        UiMotionProperty property;
        ScalarCurveKey key;
    };
    std::vector<CopiedKey> clipboard_;
    struct KeyDrag {
        UiAnimationClip before;
        float x = 0;
    };
    std::optional<KeyDrag> drag_;
};
void RegisterUiAnimationOperations(EditorOperationRegistry&, UiAnimationEditor&, std::function<DocumentSceneBase*()>);
} // namespace HIKARI::EDITOR
