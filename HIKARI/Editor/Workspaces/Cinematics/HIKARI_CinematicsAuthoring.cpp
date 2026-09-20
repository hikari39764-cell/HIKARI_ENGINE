#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"
#include <algorithm>

namespace HIKARI::EDITOR {
nlohmann::json CinematicsWorkspaceController::DescribeAuthoring(
    DocumentSceneBase &scene) const {
  const auto &document = sequenceDocumentController_.GetDocument();
  const auto &settings =
      document.GetSettings(scene.GetSceneDocument().cinematics);
  const auto *sequence = FindCinematicSequence(settings, activeSequenceId_);
  if (!sequence && !settings.sequences.empty())
    sequence = &settings.sequences.front();
  nlohmann::json result;
  if (sequence)
    SerializeCinematicSequenceJson(*sequence, result);
  return result;
}

bool CinematicsWorkspaceController::ExecuteAuthoring(
    DocumentSceneBase &scene, const nlohmann::json &command,
    std::string &message) {
  auto &document = sequenceDocumentController_.GetDocument();
  if (!document.IsExternalDocument()) {
    message = "请先创建或打开演出资产。自动化编辑使用独立文档，避免改动正在编辑"
              "的场景。";
    return false;
  }
  try {
    auto payload = DescribeAuthoring(scene);
    const auto action = command.at("action").get<std::string>();
    auto &clips = payload["tracks"][0]["clips"];
    if (action == "append_shot") {
      auto shot = command.at("value");
      if (!shot.is_object())
        throw std::runtime_error("Shot must be an object");
      uint64_t id = 1;
      for (const auto &s : clips)
        id = (std::max)(id, s.at("id").get<uint64_t>() + 1);
      shot["id"] = id;
      if (!shot.contains("camera"))
        shot["camera"] = nlohmann::json::object();
      if (!shot["camera"].contains("owned"))
        shot["camera"]["owned"] = true;
      clips.push_back(shot);
    } else if (action == "edit_shot" || action == "remove_shot") {
      const auto id = command.at("id").get<uint64_t>();
      auto it = std::find_if(clips.begin(), clips.end(), [&](const auto &s) {
        return s.at("id").template get<uint64_t>() == id;
      });
      if (it == clips.end())
        throw std::runtime_error("Shot id was not found");
      if (action == "remove_shot")
        clips.erase(it);
      else {
        it->merge_patch(command.at("value"));
        (*it)["id"] = id;
      }
    } else if (action == "set_screen_effects")
      payload["screenEffects"] = command.at("value");
    else if (action == "replace_sequence")
      payload = command.at("value");
    else if (action == "rename")
      payload["name"] = command.at("value");
    else
      throw std::runtime_error("Unknown authoring action");
    CinematicSequence candidate;
    DeserializeCinematicSequenceJson(payload, candidate);
    std::string error;
    if (!SEQUENCER::ValidatePresentationCueTrack(candidate.presentationCueTrack,
                                                 error))
      throw std::runtime_error(error);
    auto before = document.GetSettings(scene.GetSceneDocument().cinematics);
    auto &sequence = ActiveSequence(scene);
    sequence = std::move(candidate);
    activeSequenceId_ = sequence.id;
    document.RecordApplied(std::move(before), 0);
    document.SealMerge();
    previewChanged_ = true;
    message = "演出文档已更新。";
    return true;
  } catch (const std::exception &error) {
    message = error.what();
    return false;
  }
}
} // namespace HIKARI::EDITOR
