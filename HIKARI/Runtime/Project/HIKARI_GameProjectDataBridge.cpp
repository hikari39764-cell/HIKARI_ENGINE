#include "HIKARI/Project/ProjectDataView.h"
#include "HIKARI/Project/WorldView.h"

#include <string>

#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Project/Paths/HIKARI_ProjectDataFiles.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectServiceBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {

ProjectDataReadCode MapFileCode(PROJECT_DATA::ProjectDataFileCode code) noexcept {
    switch (code) {
    case PROJECT_DATA::ProjectDataFileCode::Resolved: return ProjectDataReadCode::Ok;
    case PROJECT_DATA::ProjectDataFileCode::InvalidRelativePath:
    case PROJECT_DATA::ProjectDataFileCode::NotDirectory: return ProjectDataReadCode::InvalidPath;
    case PROJECT_DATA::ProjectDataFileCode::ReparsePoint: return ProjectDataReadCode::ReparsePoint;
    case PROJECT_DATA::ProjectDataFileCode::NotFound: return ProjectDataReadCode::NotFound;
    case PROJECT_DATA::ProjectDataFileCode::TooLarge: return ProjectDataReadCode::TooLarge;
    case PROJECT_DATA::ProjectDataFileCode::InvalidUtf8: return ProjectDataReadCode::InvalidUtf8;
    case PROJECT_DATA::ProjectDataFileCode::IoFailure: return ProjectDataReadCode::IoFailure;
    }
    return ProjectDataReadCode::IoFailure;
}

} // namespace

ProjectDataView WorldView::GetProjectData() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? ProjectDataView(GameProjectWorldHandle{world->GetRuntimeIdentity()}) : ProjectDataView{};
}

bool ProjectDataView::IsValid() const noexcept { return PROJECT::INTERNAL::ResolveServiceWorld(world_) != nullptr; }

ProjectDataReadResult ProjectDataView::ReadText(std::string_view relativePath,
                                                IProjectDataTextSink& sink) const noexcept {
    RuntimeWorldAccessResult access{};
    World* world = World::ResolveRuntimeWorld(world_.value, &access);
    if (world == nullptr) {
        return {access == RuntimeWorldAccessResult::WrongThread ? ProjectDataReadCode::WrongThread
                                                                : ProjectDataReadCode::InvalidWorld,
                0u};
    }
    if (PROJECT::INTERNAL::CurrentWorld(world) == nullptr || GameProjectSdkAccess::IsWorldClosing(*world)) {
        return {ProjectDataReadCode::InvalidWorld, 0u};
    }

    try {
        const ProjectContext& projectContext = GetProjectContext();
        if (!projectContext.IsOpen()) {
            return {ProjectDataReadCode::IoFailure, 0u};
        }

        std::filesystem::path path{};
        std::string message{};
        const auto resolved =
            PROJECT_DATA::ResolveProjectDataFile(projectContext.Resolve("Data"), relativePath, path, message);
        if (resolved != PROJECT_DATA::ProjectDataFileCode::Resolved) {
            return {MapFileCode(resolved), 0u};
        }

        uint64_t byteCount = 0u;
        const auto inspected = PROJECT_DATA::InspectProjectDataTextFile(path, byteCount, message);
        if (inspected != PROJECT_DATA::ProjectDataFileCode::Resolved) {
            return {MapFileCode(inspected), byteCount};
        }

        std::string text{};
        const auto read = PROJECT_DATA::ReadProjectDataTextFile(path, byteCount, text, message);
        if (read != PROJECT_DATA::ProjectDataFileCode::Resolved) {
            return {MapFileCode(read), byteCount};
        }

        const ProjectDataReadResult result{ProjectDataReadCode::Ok, byteCount};
        try {
            sink.Receive(result, text);
        } catch (...) {
            return {ProjectDataReadCode::CallbackFailed, byteCount};
        }
        return result;
    } catch (...) {
        return {ProjectDataReadCode::IoFailure, 0u};
    }
}

} // namespace HIKARI
