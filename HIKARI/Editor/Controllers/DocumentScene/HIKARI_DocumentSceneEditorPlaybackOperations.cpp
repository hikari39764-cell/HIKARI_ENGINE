#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Editor/Authoring/HIKARI_SequencePreviewBindingResolver.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationValue.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevPlacement.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxAttachmentResolver.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

// コンソールからの再生の試し。効果や演出を World の再生サービスへ出すだけで、場面の文書は変えず、未保存にもしない。
// 編集中も Play 中も同じ World に出す（VFX は RenderSubmissionSystem が編集中も進める。演出は Play 中だけ進む）。
namespace HIKARI {
namespace {

using EDITOR::EditorOperationParameterType;
namespace JsonMath = SERIALIZATION::JSON::MATH;

constexpr float kFallbackSpawnDistance = 5.0f;
// sequence.play が使う channel。sequence.stop はこの channel だけを止める。
constexpr std::string_view kConsoleSequenceChannel = "Editor.Console";

template <class Service>
Service* FindService(DocumentSceneBase* scene) {
    return scene != nullptr ? scene->GetWorld().Services().Find<Service>() : nullptr;
}

// 再生サービスの返事を結果にする。診断は「[コード] 内容」の行で details へ。断られた時は最初の診断を message にする。
template <class Played>
EDITOR::EditorOperationResult PlaybackResult(const Played& played, const char* rejectedCode,
                                             EDITOR::EditorOperationResult accepted) {
    EDITOR::EditorOperationResult result =
        played.IsAccepted()
            ? std::move(accepted)
            : EDITOR::OperationFailure(rejectedCode, played.diagnostics.empty() ? "Playback was not accepted."
                                                                                : played.diagnostics.front().message);
    for (const auto& diagnostic : played.diagnostics) {
        result.details.push_back("[" + diagnostic.code + "] " + diagnostic.message);
    }
    return result;
}

// 視口の中心に見えている面。当たらなければカメラの前 5 m。
MATH::Vec3 ViewCenterPoint(const Camera3D& camera) {
    if (const auto hit = EDITOR::FindLookDevSurface(camera, 0.5f, 0.5f)) {
        return hit->position;
    }
    const MATH::Vec3 forward = camera.GetTarget() - camera.GetPosition();
    const float length = MATH::Length(forward);
    return length > 1.0e-5f ? camera.GetPosition() + forward * (kFallbackSpawnDistance / length) : camera.GetPosition();
}

} // namespace

void DocumentSceneEditorController::RegisterPlaybackOperations() {
    const auto add = [this](EDITOR::EditorOperationDefinition definition, std::vector<std::string> terms) {
        definition.permission = EDITOR::EditorOperationPermission::ModifyEditorState;
        definition.searchTerms = std::move(terms);
        operationRegistry_.Register(std::move(definition), EDITOR::EditorOperationKind::Session, false);
    };

    add({"vfx.spawn",
         "Play a VFX effect in the live world (editing or Play) to look at it. Nothing is added to the scene document. "
         "at: omitted = the surface at the view centre (5 m ahead if nothing is hit), [x, y, z] = world position, "
         "integer = scene object id (starts at the object; emitters attached to Owner or OwnerSocket follow it). "
         "vfx.clear stops what this spawned.",
         "runtime",
         {{"asset", EditorOperationParameterType::String, true, "VFX effect asset: GUID or project path"},
          {"at",
           EditorOperationParameterType::Json,
           false,
           "Where: [x, y, z] world position or a scene object id; default is the view centre surface",
           {{"oneOf",
             nlohmann::json::array({EDITOR::EditorOperationVec3Schema(), {{"type", "integer"}, {"minimum", 1}}})}}},
          {"loop", EditorOperationParameterType::Bool, false,
           "true loops, false plays once; omitted keeps the effect's own setting"}},
         [this](const nlohmann::json& p) -> EDITOR::EditorOperationResult {
             DocumentSceneBase* scene = aiOperationScene_;
             if (scene == nullptr) {
                 return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
             }
             VFX::VfxPlaybackService* playback = FindService<VFX::VfxPlaybackService>(scene);
             if (playback == nullptr) {
                 return EDITOR::OperationFailure("VFX_UNAVAILABLE", "This world has no VFX playback.");
             }
             const std::string assetText = p.at("asset").get<std::string>();
             const AssetRecord* record =
                 EDITOR::FindOperationAsset(scene->GetAssetDatabase(), assetText, AssetType::VfxEffect);
             if (record == nullptr) {
                 return EDITOR::OperationFailure("ASSET_NOT_FOUND", "No VFX effect asset: " + assetText);
             }
             World& world = scene->GetWorld();
             VFX::VfxPlayRequest request{};
             request.assetGuid = record->guid;
             const nlohmann::json at = p.value("at", nlohmann::json());
             if (at.is_array()) {
                 request.transform.position = JSONREAD::Vec3Or(at, {});
             } else if (at.is_number_integer()) {
                 const GameObject* owner = world.FindObject(SceneObjectId{at.get<uint64_t>()});
                 if (owner == nullptr) {
                     return EDITOR::OperationFailure("OBJECT_NOT_FOUND", "No scene object has this id.");
                 }
                 request.ownerObject = owner->GetRuntimeHandle();
                 (void)VFX::ResolveVfxAnimationAttachment(world, request.ownerObject, {}, request.transform);
             } else {
                 request.transform.position = ViewCenterPoint(scene->GetCamera());
             }
             if (p.contains("loop")) {
                 request.loopOverride = p.at("loop").get<bool>();
             }
             const VFX::VfxPlayResult played = playback->PlayDetailed(request);
             if (played.IsAccepted()) {
                 // 再生サービスが替わっていたら（別の場面）古い記録は捨てる。終わった効果も数えない。
                 if (consoleVfxNamespace_ != playback->GetPlaybackNamespace()) {
                     consoleVfx_.clear();
                     consoleVfxNamespace_ = playback->GetPlaybackNamespace();
                 }
                 std::erase_if(consoleVfx_,
                               [playback](VFX::VfxPlaybackHandle handle) { return !playback->IsActive(handle); });
                 consoleVfx_.push_back(played.handle);
             }
             return PlaybackResult(
                 played, "VFX_REJECTED",
                 EDITOR::OperationOk(
                     "Playing " + TEXT::PathToUtf8(record->sourcePath) + ".",
                     {{"asset", record->guid.value}, {"position", JsonMath::ToJsonArray(request.transform.position)}}));
         }},
        {"vfx", "effect", "particle", "spawn", "play effect", "特效", "播放特效"});

    add({"vfx.clear",
         "Stop every effect that vfx.spawn started.",
         "runtime",
         {},
         [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
             VFX::VfxPlaybackService* playback = FindService<VFX::VfxPlaybackService>(aiOperationScene_);
             size_t stopped = 0;
             if (playback != nullptr && consoleVfxNamespace_ == playback->GetPlaybackNamespace()) {
                 for (const VFX::VfxPlaybackHandle handle : consoleVfx_) {
                     stopped += playback->Stop(handle) ? 1u : 0u;
                 }
             }
             consoleVfx_.clear();
             return EDITOR::OperationOk("Stopped " + std::to_string(stopped) + " effects.", {{"stopped", stopped}});
         }},
        {"vfx", "effect", "particle", "clear", "stop", "清除特效"});

    add({"sequence.play",
         "Play a Sequence asset while Play runs, on the console's own channel (a new one replaces the previous "
         "console sequence). Slots are bound from the scene's Sequence Player components for that asset and from "
         "camera objects, as the Cinematics preview does. sequence.stop stops it.",
         "runtime",
         {{"asset", EditorOperationParameterType::String, true, "Sequence asset: GUID or project path"},
          {"from", EditorOperationParameterType::Float, false, "Start time in seconds (default 0)", {{"minimum", 0}}}},
         [this](const nlohmann::json& p) -> EDITOR::EditorOperationResult {
             DocumentSceneBase* scene = aiOperationScene_;
             if (scene == nullptr) {
                 return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
             }
             // 演出の再生サービスが進むのは Play 中で、Cinematics の機能が有効な時だけ。
             if (!scene->IsRuntimePlayActive()) {
                 return EDITOR::OperationFailure("PLAY_NOT_RUNNING",
                                                 "Sequences advance only while Play runs: start Play first.");
             }
             SEQUENCER::SequencePlaybackService* playback = FindService<SEQUENCER::SequencePlaybackService>(scene);
             SequenceAssetStore* store = playback != nullptr ? playback->GetAssetStore() : nullptr;
             if (store == nullptr || !scene->IsRuntimeFeatureActive(RuntimeFeatureIds::Cinematics)) {
                 return EDITOR::OperationFailure("SEQUENCE_UNAVAILABLE",
                                                 "Sequences do not run in this world: the Cinematics feature is off.");
             }
             const std::string assetText = p.at("asset").get<std::string>();
             const AssetRecord* record =
                 EDITOR::FindOperationAsset(scene->GetAssetDatabase(), assetText, AssetType::Sequence);
             if (record == nullptr) {
                 return EDITOR::OperationFailure("ASSET_NOT_FOUND", "No Sequence asset: " + assetText);
             }
             std::string error;
             const std::shared_ptr<const SequenceAsset> asset = store->Load(record->guid, &error);
             if (asset == nullptr) {
                 return EDITOR::OperationFailure("SEQUENCE_LOAD_FAILED", error);
             }
             SEQUENCER::SequencePlayRequest request{};
             request.assetGuid = record->guid;
             request.channel = kConsoleSequenceChannel;
             request.startTimeSeconds = EDITOR::ReadEditorOperationFloat(p, "from", 0.0f);
             EDITOR::RestoreSequencePreviewBindings(scene->GetSceneDocument(), record->guid, asset->sequence,
                                                    request.bindings);
             return PlaybackResult(
                 playback->PlayDetailed(request), "SEQUENCE_REJECTED",
                 EDITOR::OperationOk("Playing " + TEXT::PathToUtf8(record->sourcePath) + ".",
                                     {{"asset", record->guid.value}, {"duration", asset->sequence.durationSeconds}}));
         }},
        {"sequence", "cinematic", "timeline", "play", "演出", "播放演出"});

    add({"sequence.stop",
         "Stop the sequence that sequence.play started.",
         "runtime",
         {},
         [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
             SEQUENCER::SequencePlaybackService* playback =
                 FindService<SEQUENCER::SequencePlaybackService>(aiOperationScene_);
             const SEQUENCER::SequencePlaybackHandle handle =
                 playback != nullptr ? playback->FindActiveByChannel(kConsoleSequenceChannel)
                                     : SEQUENCER::SequencePlaybackHandle{};
             const bool stopped = handle.IsValid() && playback->Stop(handle);
             return EDITOR::OperationOk(stopped ? "Sequence stopped." : "No console sequence is playing.",
                                        {{"stopped", stopped}});
         }},
        {"sequence", "cinematic", "timeline", "stop", "停止演出"});
}

} // namespace HIKARI
