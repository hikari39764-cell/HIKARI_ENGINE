#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Documents/HIKARI_AnimationStateMachineEditorDocument.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

// アニメーションステートマシンワークスペースコントローラークラス AnimationStateMachineWorkspaceController を定義しています。
// アニメーションステートマシンエディタ内のワークスペースを管理するために使用されます。以下の機能を提供します：
// 1. ドキュメント管理：新しいドキュメントの作成、既存のドキュメントのオープンが可能です。
// 2. ワークスペースインターフェースの描画：ドッキングスペース、グラフウィンドウ、パラメータウィンドウ、トランジションウィンドウ、詳細情報ウィンドウ、診断ウィンドウの描画を含みます。
// 3. 選択管理：状態、パラメータ、トランジションの選択が可能で、追加、削除、編集などの関連操作を提供します。
// 4. 元に戻す/やり直し機能：元に戻すおよびやり直し操作をサポートします。
// 5. ドキュメントの保存と状態の確認：ドキュメントを保存し、ドキュメントが変更されているか、元に戻すまたはやり直しが可能かを確認できます。
// 6. ランタイムデバッグ：ランタイムデバッグ情報の描画が可能で、ランタイムスナップショットの解析を行います。
// 7. コマンドのバインディング：ドキュメント関連のコマンドをコマンドルーターにバインドできます。
// 8. ステートマシン定義の変更履歴の記録：ステートマシン定義の変更を記録し、元に戻す/やり直し操作を行うことができます。
// 9. グラフビューの管理：グラフビューのパン、ズーム、コンテキストメニューの管理が可能です。
// 10. 未保存ドキュメントのプロンプト：データの損失を招く可能性のある操作を実行する際、保存または変更を破棄するかどうかを尋ねます。

namespace HIKARI {
class DocumentSceneBase;
}
namespace HIKARI::ANIMATION {
struct AnimationStateMachineRuntimeSnapshot;
}

namespace HIKARI::EDITOR {

    class EditorWorkspaceHost;

    /// アニメーションステートマシン編集ワークスペースの描画結果。
    struct AnimationStateMachineWorkspaceResult {
        bool exitToSceneRequested = false;
        std::string statusMessage{};
    };

    /// アニメーションステートマシン編集用ワークスペースを管理する。
    ///
    /// ドキュメントのライフサイクル、グラフ編集UI、選択状態、Undo/Redo、
    /// 保存確認、およびランタイムデバッグ表示を統括する。
    class AnimationStateMachineWorkspaceController {
    public:
        /// ワークスペースのアクティブ化要求を適用する。
        /// アセットが指定されている場合は、対象ドキュメントを開く。
        void ApplyWorkspaceActivation(
            DocumentSceneBase& scene,
            const EditorWorkspaceActivation& activation);

        void DrawDockSpace(bool resetDefaultDockLayout) const;

        /// ワークスペース全体を描画し、そのフレームで発生した要求を返す。
        AnimationStateMachineWorkspaceResult Draw(
            DocumentSceneBase& scene,
            EditorCommandRouter& commandRouter);

        bool IsDocumentOpen() const noexcept;
        bool IsDocumentDirty() const noexcept;
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;

        /// 現在のドキュメントを保存する。
        /// 失敗した場合はoutMessageに理由を設定する。
        bool Save(DocumentSceneBase& scene, std::string& outMessage);

        bool Undo(std::string& outMessage);
        bool Redo(std::string& outMessage);

        /// このワークスペースで使用するドキュメントコマンドを登録する。
        void BindDocumentCommands(
            EditorCommandRouter& commandRouter,
            DocumentSceneBase& scene);

    private:
        enum class SelectionKind : uint8_t {
            None,
            State,
            Parameter,
            Transition,
        };

        /// グラフメニューから、そのフレーム中に発行された操作要求。
        struct GraphMenuRequests {
            bool addStateAtCenter = false;
            bool frameAll = false;
            bool frameSelection = false;
            bool resetView = false;
        };

        /// 未保存確認後に実行するドキュメント操作。
        enum class PendingDocumentAction : uint8_t {
            None,
            NewDocument,
            OpenAsset,
            ExitToScene,
        };

        // -------------------------------------------------------------------------
        // Workspace windows
        // -------------------------------------------------------------------------

        void DrawGraphWindow(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result,
            EditorCommandRouter& commandRouter);

        void DrawGraphMenuBar(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result,
            GraphMenuRequests& requests,
            EditorCommandRouter& commandRouter);

        void DrawParametersWindow();
        void DrawTransitionsWindow();
        void DrawDetailsWindow(DocumentSceneBase& scene);
        void DrawDiagnosticsWindow(DocumentSceneBase& scene);
        void DrawRuntimeDebug(DocumentSceneBase& scene);

        /// 選択中ステートのモーション設定UIを描画する。
        void DrawSelectedStateMotionEditor(
            DocumentSceneBase& scene,
            ANIMATION::AnimationState& state);

        // -------------------------------------------------------------------------
        // Document lifecycle
        // -------------------------------------------------------------------------

        /// ドキュメントが未保存の場合は確認ダイアログを要求し、
        /// 操作をpending状態として保持する。
        void RequestDocumentAction(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result,
            GraphMenuRequests& requests,
            PendingDocumentAction action,
            AssetGuid assetGuid = {});

        /// 未保存確認済みのドキュメント操作を実行する。
        void ExecuteDocumentAction(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result,
            GraphMenuRequests& requests,
            PendingDocumentAction action,
            const AssetGuid& assetGuid);

        void DrawUnsavedDocumentDialog(
            DocumentSceneBase& scene,
            AnimationStateMachineWorkspaceResult& result,
            GraphMenuRequests& requests);

        // -------------------------------------------------------------------------
        // Graph editing
        // -------------------------------------------------------------------------

        void AddState();
        void AddStateAt(float graphX, float graphY);
        void DeleteSelectedState();

        void AddParameter(ANIMATION::AnimationParameterType type);
        void DeleteSelectedParameter();

        void AddTransition();

        /// sourceからtargetへのトランジションを作成する。
        /// 無効なIDまたは重複する組み合わせは追加しない。
        void AddTransitionBetween(
            ANIMATION::AnimationStateId source,
            ANIMATION::AnimationStateId target);

        void DeleteSelectedTransition();

        // -------------------------------------------------------------------------
        // Selection
        // -------------------------------------------------------------------------

        void SelectEntryState() noexcept;

        /// 削除などで無効になった選択IDを解除し、
        /// selectionKindと選択IDの整合性を維持する。
        void NormalizeSelection() noexcept;

        ANIMATION::AnimationState* SelectedState() noexcept;
        ANIMATION::AnimationParameterDefinition* SelectedParameter() noexcept;
        ANIMATION::AnimationStateTransition* SelectedTransition() noexcept;

        // -------------------------------------------------------------------------
        // History and runtime debug
        // -------------------------------------------------------------------------

        /// 編集前の定義をUndo履歴へ記録する。
        /// 同一のmergeGroupを持つ連続操作は統合できる。
        void RecordMutation(
            ANIMATION::AnimationStateMachineDefinition before,
            uint64_t mergeGroup = 0u);

        /// 現在選択されているランタイムオブジェクトのスナップショットを取得する。
        /// 利用できない場合はnullptrを返す。
        const ANIMATION::AnimationStateMachineRuntimeSnapshot*
            ResolveRuntimeDebugSnapshot(DocumentSceneBase& scene);

        AnimationStateMachineEditorDocument document_{};

        SelectionKind selectionKind_ = SelectionKind::None;
        ANIMATION::AnimationStateId selectedStateId_{};
        ANIMATION::AnimationParameterId selectedParameterId_{};
        ANIMATION::AnimationTransitionId selectedTransitionId_{};

        float graphPanX_ = 0.0f;
        float graphPanY_ = 0.0f;
        float graphZoom_ = 1.0f;

        ANIMATION::AnimationStateId graphContextStateId_{};
        ANIMATION::AnimationTransitionId graphContextTransitionId_{};
        ANIMATION::AnimationStateId transitionDragSourceStateId_{};
        float graphContextX_ = 0.0f;
        float graphContextY_ = 0.0f;

        /// ドラッグ操作開始前の定義。ドラッグ終了時の履歴登録に使用する。
        std::optional<ANIMATION::AnimationStateMachineDefinition>
            graphDragBefore_{};

        RuntimeObjectHandle runtimeDebugObject_{};

        PendingDocumentAction pendingDocumentAction_ =
            PendingDocumentAction::None;
        AssetGuid pendingDocumentAssetGuid_{};
        bool requestUnsavedDocumentDialog_ = false;

        std::string statusMessage_{};
    };

} // namespace HIKARI::EDITOR