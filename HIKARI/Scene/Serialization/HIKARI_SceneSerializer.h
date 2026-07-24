#pragma once

#include <string>

namespace HIKARI {

    struct SceneDocument;

    class SceneSerializer {
    public:
        bool LoadFromFile(const std::string& path, SceneDocument& outDocument) const;
        bool SaveToFile(const std::string& path, const SceneDocument& document) const;
    };

} // namespace HIKARI
