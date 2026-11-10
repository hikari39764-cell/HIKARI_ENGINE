#pragma once

#include <string_view>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/ProjectDataTypes.h"
#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

// Text is borrowed only for this callback. Parse project-owned formats in
// project code; the Engine treats Data files as bounded UTF-8 text.
class IProjectDataTextSink {
public:
    virtual ~IProjectDataTextSink() = default;
    virtual void Receive(const ProjectDataReadResult& result, std::string_view utf8Text) = 0;
};

// Non-owning view tied to one World identity. ReadText is synchronous and
// owner-thread only; it never writes project files or save data.
class ProjectDataView {
public:
    ProjectDataView() = default;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API ProjectDataReadResult ReadText(std::string_view relativePath,
                                                               IProjectDataTextSink& sink) const noexcept;
    // Reads the text of an asset whose type a module declares in its manifest (assetTypes[].typeId), by GUID.
    // NotFound when the asset is missing or belongs to another type. Works in the Editor and in exported games.
    HIKARI_GAME_PROJECT_SDK_API ProjectDataReadResult ReadModuleAsset(std::string_view assetGuid,
                                                                      std::string_view moduleAssetTypeId,
                                                                      IProjectDataTextSink& sink) const noexcept;
    // Changes whenever the text of that asset changes (for example when it is saved again during Play in the
    // Editor), and is zero while the asset cannot be read. Cheap enough to compare every frame.
    HIKARI_GAME_PROJECT_SDK_API uint64_t GetModuleAssetRevision(std::string_view assetGuid,
                                                                std::string_view moduleAssetTypeId) const noexcept;

private:
    friend class WorldView;

    explicit ProjectDataView(GameProjectWorldHandle world) noexcept : world_(world) {}

    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
