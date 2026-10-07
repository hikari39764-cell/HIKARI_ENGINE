#include "Assets/Models/Loading/HIKARI_AssimpModelLoader.h"
#include "Assets/Models/Loading/HIKARI_GltfModelLoader.h"
#include "Assets/Models/Loading/HIKARI_ObjModelLoader.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Views/HIKARI_EditorViewCapture.h"
#endif

namespace HIKARI::ASSETS::MODELS {

// Source-model import belongs to the Editor authoring product. Runtime
// and isolated Runtime contract hosts consume cooked model artifacts.
bool LoadAssimpModelSource(ModelAsset&) { return false; }
bool LoadGltfModelSource(ModelAsset&, std::vector<AssetImportInputFile>*) { return false; }
bool LoadObjModelSource(ModelAsset&, std::vector<AssetImportInputFile>*) { return false; }

} // namespace HIKARI::ASSETS::MODELS

#if defined(HIKARI_WITH_EDITOR)
namespace HIKARI::RENDER3D::EDITORVIEW {

// Isolated Runtime contract hosts link EditorRelease runtime libraries but
// do not own the Editor view-capture service or accept capture requests.
void RecordViewCapture(RenderViewId, bool, uint64_t, ID3D12Resource*, D3D12_RESOURCE_STATES, uint32_t, uint32_t,
                       const ResolvedCameraFrame*, uint64_t) {}

} // namespace HIKARI::RENDER3D::EDITORVIEW
#endif
