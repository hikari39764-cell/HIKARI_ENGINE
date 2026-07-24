#include "Scene/Serialization/Systems/HIKARI_SceneSystemsJson.h"

#include <string>
#include <utility>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::SCENE::SERIALIZATION {

    void SerializeSceneSystemsJson(const std::vector<SceneSystemData>& systems, nlohmann::json& out) {

        out = nlohmann::json::array();
        for (const SceneSystemData& system : systems) {
            out.push_back({ { "systemId", system.systemId },
                            { "enabled", system.enabled },
                            { "executionOrder", system.executionOrder },
                            { "settings", system.settings.is_object() ? system.settings : nlohmann::json::object() } });
        }
    }

    void DeserializeSceneSystemsJson(const nlohmann::json& node, std::vector<SceneSystemData>& outSystems) {

        outSystems.clear();
        if (!node.is_array()) {
            return;
        }

        for (const nlohmann::json& systemNode : node) {
            if (!systemNode.is_object()) {
                continue;
            }

            SceneSystemData system{};
            system.systemId = systemNode.value("systemId", std::string{});
            if (system.systemId.empty()) {
                continue;
            }
            system.enabled = systemNode.value("enabled", true);
            system.executionOrder = systemNode.value("executionOrder", 0);
            system.settings = systemNode.value("settings", nlohmann::json::object());
            if (!system.settings.is_object()) {
                system.settings = nlohmann::json::object();
            }
            outSystems.push_back(std::move(system));
        }
    }

} // namespace HIKARI::SCENE::SERIALIZATION
