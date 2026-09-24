#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"
#include <algorithm>

namespace HIKARI::EDITOR {
namespace {
enum class EditKind { AppendShot, EditShot, RemoveShot, ScreenEffects, ReplaceSequence, Rename };
struct EditDefinition {
    const char* name;
    EditKind kind;
    bool needsId;
    const char* valueType;
};
constexpr EditDefinition edits[]{
    {"append_shot", EditKind::AppendShot, false, "object"},
    {"edit_shot", EditKind::EditShot, true, "object"},
    {"remove_shot", EditKind::RemoveShot, true, nullptr},
    {"set_screen_effects", EditKind::ScreenEffects, false, "array"},
    {"replace_sequence", EditKind::ReplaceSequence, false, "object"},
    {"rename", EditKind::Rename, false, "string"},
};
// 保存時に消える未知のキーを成功扱いしない。項目名は serializer の出力を正とする。
void ValidateSuppliedStructure(const nlohmann::json& supplied, const nlohmann::json& canonical,
                               const std::string& path) {
    if (supplied.is_object()) {
        for (const auto& [key, value] : supplied.items()) {
            if (!canonical.is_object() || !canonical.contains(key))
                throw std::runtime_error("Unknown or unsupported field: " + path + "." + key +
                                         ". Use cinematics.schema for editable fields.");
            ValidateSuppliedStructure(value, canonical[key], path + "." + key);
        }
    } else if (supplied.is_array()) {
        if (!canonical.is_array() || supplied.size() > canonical.size())
            throw std::runtime_error("Unsupported entries in " + path);
        for (size_t i = 0; i < supplied.size(); ++i)
            ValidateSuppliedStructure(supplied[i], canonical[i], path + "[" + std::to_string(i) + "]");
    }
}

} // namespace
nlohmann::json CinematicsWorkspaceController::AuthoringCommandSchema() {
    using Json = nlohmann::json;
    auto variants = Json::array();
    for (const auto& edit : edits) {
        Json shape{{"type", "object"},
                   {"required", Json::array({"action"})},
                   {"additionalProperties", false},
                   {"properties", {{"action", {{"type", "string"}, {"enum", Json::array({edit.name})}}}}}};
        if (edit.needsId) {
            shape["required"].push_back("id");
            shape["properties"]["id"] = {{"type", "integer"}, {"minimum", 1}};
        }
        if (edit.valueType) {
            shape["required"].push_back("value");
            shape["properties"]["value"] = {{"type", edit.valueType}};
        }
        variants.push_back(std::move(shape));
    }
    return {{"oneOf", std::move(variants)}};
}
bool CinematicsWorkspaceController::DeleteSelectedClip(CinematicSequence& sequence) {
    size_t removed = 0;
    const auto selected = [this](const auto& clip) { return clip.id == selectedId_; };
    if (selectionKind_ == SelectionKind::Shot)
        removed = std::erase_if(sequence.cameraCutTrack.clips, selected);
    else if (selectionKind_ == SelectionKind::Cue)
        removed = std::erase_if(sequence.presentationCueTrack.clips, selected);
    else if (selectionKind_ == SelectionKind::Effect)
        removed = std::erase_if(sequence.screenEffects, selected);
    if (!removed)
        return false;
    selectedId_ = 0;
    curveView_.selectedKey = -1;
    channelOwner_ = 0;
    draggingId_ = 0;
    return true;
}

nlohmann::json CinematicsWorkspaceController::DescribeAuthoring(DocumentSceneBase& scene) const {
    const auto& document = sequenceDocumentController_.GetDocument();
    const auto& settings = document.GetSettings(scene.GetSceneDocument().cinematics);
    const auto* sequence = FindCinematicSequence(settings, activeSequenceId_);
    if (!sequence && !settings.sequences.empty())
        sequence = &settings.sequences.front();
    nlohmann::json result;
    if (sequence)
        SerializeCinematicSequenceJson(*sequence, result);
    return result;
}

bool CinematicsWorkspaceController::ExecuteAuthoring(DocumentSceneBase& scene, const nlohmann::json& command,
                                                     std::string& message) {
    auto& document = sequenceDocumentController_.GetDocument();
    if (!document.IsExternalDocument()) {
        message = TEXT::Localize("cine.openForAutomation");
        return false;
    }
    try {
        auto payload = DescribeAuthoring(scene);
        const auto action = command.at("action").get<std::string>();
        const auto definition =
            std::find_if(std::begin(edits), std::end(edits), [&](const auto& edit) { return action == edit.name; });
        if (definition == std::end(edits))
            throw std::runtime_error("Unknown authoring action");
        if (definition->needsId && !command.contains("id"))
            throw std::runtime_error("Missing required parameter: command.id");
        if (definition->valueType && !command.contains("value"))
            throw std::runtime_error("Missing required parameter: command.value");
        const auto kind = definition->kind;
        auto& clips = payload["tracks"][0]["clips"];
        if (kind == EditKind::AppendShot) {
            auto shot = command.at("value");
            if (!shot.is_object())
                throw std::runtime_error("Shot must be an object");
            uint64_t id = 1;
            for (const auto& s : clips)
                id = (std::max)(id, s.at("id").get<uint64_t>() + 1);
            shot["id"] = id;
            if (!shot.contains("camera"))
                shot["camera"] = nlohmann::json::object();
            if (!shot["camera"].contains("owned"))
                shot["camera"]["owned"] = true;
            clips.push_back(shot);
        } else if (kind == EditKind::EditShot || kind == EditKind::RemoveShot) {
            const auto id = command.at("id").get<uint64_t>();
            auto it = std::find_if(clips.begin(), clips.end(),
                                   [&](const auto& s) { return s.at("id").template get<uint64_t>() == id; });
            if (it == clips.end())
                throw std::runtime_error("Shot id was not found");
            if (kind == EditKind::RemoveShot)
                clips.erase(it);
            else {
                const auto& patch = command.at("value");
                it->merge_patch(patch);
                (*it)["id"] = id;
            }
        } else if (kind == EditKind::ScreenEffects)
            payload["screenEffects"] = command.at("value");
        else if (kind == EditKind::ReplaceSequence)
            payload = command.at("value");
        else if (kind == EditKind::Rename)
            payload["name"] = command.at("value");
        else
            throw std::runtime_error("Unknown authoring action");
        CinematicSequence candidate;
        DeserializeCinematicSequenceJson(payload, candidate);
        std::string error;
        if (!SEQUENCER::ValidatePresentationCueTrack(candidate.presentationCueTrack, error))
            throw std::runtime_error(error);
        nlohmann::json normalized;
        SerializeCinematicSequenceJson(candidate, normalized);
        ValidateSuppliedStructure(payload, normalized, "sequence");
        if (normalized == DescribeAuthoring(scene)) {
            message = "Sequence already has these values; no edit was recorded.";
            return true;
        }
        auto before = document.GetSettings(scene.GetSceneDocument().cinematics);
        auto& sequence = ActiveSequence(scene);
        sequence = std::move(candidate);
        activeSequenceId_ = sequence.id;
        document.RecordApplied(std::move(before), 0);
        document.SealMerge();
        previewChanged_ = true;
        message = TEXT::Localize("cine.documentUpdated");
        return true;
    } catch (const std::exception& error) {
        message = error.what();
        return false;
    }
}
} // namespace HIKARI::EDITOR
