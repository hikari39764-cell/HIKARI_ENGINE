#include "Input/Assets/HIKARI_InputTrack.h"

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"

#include <algorithm>

namespace HIKARI::INPUT {
namespace {

bool ReadValue(const nlohmann::json& node, std::array<float, 2>& outValue, std::string& outDiagnostic,
               const std::string& path) {
    const auto finite = [](const nlohmann::json& number) {
        return number.is_number() && JSONSTRICT::IsFloatRange(number.get<double>());
    };
    if (finite(node)) {
        outValue = {node.get<float>(), 0.0f};
        return true;
    }
    if (node.is_array() && node.size() == 2u && finite(node[0]) && finite(node[1])) {
        outValue = {node[0].get<float>(), node[1].get<float>()};
        return true;
    }
    return JSONSTRICT::Fail(outDiagnostic, path + " must be a finite number or [x, y]");
}

} // namespace

bool ReadInputTrack(const nlohmann::json& root, InputTrack& outTrack, std::string& outDiagnostic) {
    outTrack = {};
    if (!JSONSTRICT::RequireFormatVersion(root, kInputTrackFormatVersion, outDiagnostic))
        return false;
    if (root.value("assetType", std::string{}) != "InputTrack")
        return JSONSTRICT::Fail(outDiagnostic, "root.assetType must be \"InputTrack\"");
    if (!root.contains("length"))
        return JSONSTRICT::Fail(outDiagnostic, "root.length is required");
    if (!JSONSTRICT::ReadUint64(root, "length", 0u, outTrack.length, outDiagnostic, "root"))
        return false;
    if (outTrack.length == 0u || outTrack.length > kMaximumInputTrackTicks)
        return JSONSTRICT::Fail(outDiagnostic,
                                "root.length must be 1 to " + std::to_string(kMaximumInputTrackTicks) + " ticks");
    const auto events = root.find("events");
    if (events == root.end() || !events->is_array())
        return JSONSTRICT::Fail(outDiagnostic, "root.events must be an array");
    if (events->size() > kMaximumInputTrackEvents)
        return JSONSTRICT::Fail(outDiagnostic,
                                "root.events allows at most " + std::to_string(kMaximumInputTrackEvents) + " changes");
    outTrack.events.reserve(events->size());
    for (size_t index = 0u; index < events->size(); ++index) {
        const nlohmann::json& node = (*events)[index];
        const std::string path = "root.events[" + std::to_string(index) + "]";
        if (!node.is_object())
            return JSONSTRICT::Fail(outDiagnostic, path + " must be an object");
        InputTrackEvent event{};
        if (!node.contains("tick"))
            return JSONSTRICT::Fail(outDiagnostic, path + ".tick is required");
        if (!JSONSTRICT::ReadUint64(node, "tick", 0u, event.tick, outDiagnostic, path))
            return false;
        if (event.tick >= outTrack.length)
            return JSONSTRICT::Fail(outDiagnostic, path + ".tick must be below root.length");
        if (!JSONSTRICT::ReadString(node, "action", {}, event.actionId, outDiagnostic, path))
            return false;
        if (event.actionId.empty())
            return JSONSTRICT::Fail(outDiagnostic, path + ".action is required");
        if (!node.contains("value"))
            return JSONSTRICT::Fail(outDiagnostic, path + ".value is required");
        if (!ReadValue(node["value"], event.value, outDiagnostic, path + ".value"))
            return false;
        outTrack.events.push_back(std::move(event));
    }
    std::stable_sort(outTrack.events.begin(), outTrack.events.end(),
                     [](const InputTrackEvent& lhs, const InputTrackEvent& rhs) { return lhs.tick < rhs.tick; });
    return true;
}

bool LoadInputTrack(const std::filesystem::path& path, InputTrack& outTrack, std::string& outDiagnostic) {
    nlohmann::json root;
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &outDiagnostic))
        return false;
    return ReadInputTrack(root, outTrack, outDiagnostic);
}

} // namespace HIKARI::INPUT
