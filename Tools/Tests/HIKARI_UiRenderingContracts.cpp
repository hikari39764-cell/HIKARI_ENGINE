#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Cooking/HIKARI_AssetCooker.h"
#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "HIKARI/Project/FrameContext.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Workspaces/TextStyle/HIKARI_TextStyleWorkspaceController.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Ui/HIKARI_GameUiEffectRenderer.h"
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"
#include "RenderCore/Text/HIKARI_SdfTextLayout.h"
#include "Render3D/Ui/HIKARI_GameUiTextLayout.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"
#include "Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_WorldMassStore.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/Ui/HIKARI_UiSceneSystem.h"
#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "RenderCore/Texture/HIKARI_DxTexture.h"
#include <d3d12sdklayers.h>
#include <DirectXTex.h>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <wincodec.h>
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"
#include "Tools/Tests/Internal/HIKARI_GpuContractDevice.h"

using namespace HIKARI;
using Microsoft::WRL::ComPtr;
namespace {
GAME_UI::GameUiFrameSource LoadCanvas(const std::filesystem::path& project, const std::filesystem::path& scenePath,
                                      const std::filesystem::path& output) {
    AssetDatabase database;
    assert(database.Initialize(project) && database.ScanAssets(true));
    for (auto type : {AssetType::Font, AssetType::TextStyle, AssetType::UiAnimation, AssetType::Scene})
        for (const auto* record : database.CollectByType(type)) {
            const auto guid = record->guid;
            assert(database.ImportAsset(guid));
        }
    SceneDocument scene;
    assert(SceneSerializer{}.LoadFromFile(TEXT::PathToUtf8(scenePath), scene));
    ComponentRegistry registry;
    WorldMassSchemaRegistry schemas;
    SystemTypeRegistry systems;
    ComponentSystemPolicy policy;
    auto feature = RuntimeFeatureContextAccess::Create(registry, schemas, systems, policy);
    assert(CreateUiRuntimeFeature()->Register(feature));
    World world;
    TextAssetStore texts;
    texts.Configure(&database);
    assert(world.Services().Register(texts));
    std::unordered_map<uint64_t, GameObject*> objects;
    for (const auto& data : scene.objects) {
        auto* object = world.CreateObject(data.name);
        assert(object && object->SetDocumentId(data.id));
        objects.emplace(data.id.value, object);
        for (const auto& component : data.components) {
            const auto* info = registry.Find(component.type);
            // この検証の対象は UI。カメラ等の非 UI コンポーネントは描画しない。
            if (!info) {
                continue;
            }
            auto instance = info->factory();
            instance->Deserialize(component.properties);
            assert(object->AddComponentInstance(std::move(instance)));
        }
    }
    for (const auto& data : scene.objects)
        if (data.parent)
            assert(objects.at(data.id.value)->SetParent(objects.at(data.parent->value)));
    GAME_UI::GameUiPresentationService presentation;
    presentation.SetProjectRoot(project);
    presentation.SetAssetDatabase(&database);
    UiSceneSystem system;
    FrameContext frame{};
    frame.frameIndex = 1;
    system.BuildPresentation(world, frame, presentation);
    presentation.Tick(1);
    auto result = presentation.GetFrameSource();
    assert(result.HasDraws() && !result.texts.empty());
    for (const auto& text : result.texts)
        assert(text.fontRevision != 0);

    ASSETS::COOKING::AssetCookOptions cook;
    cook.projectRoot = project;
    cook.contentRoot = output / "Cooked";
    const auto* record = database.FindByPath(scenePath);
    assert(record);
    cook.startupSceneGuid = record->guid.value;
    cook.sceneGuids = {record->guid.value};
    const auto cooked = ASSETS::COOKING::AssetCooker{}.Cook(cook);
    if (!cooked.success)
        std::cerr << cooked.message << '\n';
    assert(cooked.success);
    std::cout << "UI scene: " << result.sprites.size() << " sprites, " << result.texts.size()
              << " text requests; cooked " << cooked.assetCount << " assets\n";
    return result;
}
void CheckAtlasReload(const std::filesystem::path& font, const std::filesystem::path& output) {
    namespace TR = TEXT_RENDERING;
    const auto copy = output / "font-reload.fixture";
    std::filesystem::copy_file(font, copy, std::filesystem::copy_options::overwrite_existing);
    TR::SdfTextLayoutRequest request;
    request.absoluteFontAssetPath = TEXT::PathToUtf8(copy);
    request.fontRevision = 1;
    request.utf8Text = "ABC";
    request.fontHeight = 32;
    request.maxWidth = 400;
    request.maxHeight = 64;
    std::vector<TR::SdfTextGlyphDraw> a, b;
    TR::SdfTextLayoutLease oldLease, newLease;
    assert(TR::BuildSdfTextGlyphDraws({&request, 1}, a, nullptr, 128, &oldLease));
    assert(!a.empty() && oldLease);
    std::vector<TR::SdfTextCaretStop> stops;
    std::vector<TR::SdfTextGlyphDraw> measured;
    request.utf8Text = "A \xe6\x97\xa5";
    assert(TR::BuildSdfTextGlyphDraws({&request, 1}, measured, nullptr, 128, nullptr, &stops));
    assert(stops.size() == 4 && stops[0].byteOffset == 0 && stops[1].byteOffset == 1 && stops[2].byteOffset == 2 &&
           stops[3].byteOffset == 5 && stops[2].x > stops[1].x);
    request.utf8Text.clear();
    assert(TR::BuildSdfTextGlyphDraws({&request, 1}, measured, nullptr, 128, nullptr, &stops));
    assert(measured.empty() && stops.size() == 1 && stops[0].byteOffset == 0);
    request.utf8Text = "ABC";
    {
        std::ofstream file(copy, std::ios::binary | std::ios::app);
        file.put('\0');
    }
    request.fontRevision = 2;
    assert(TR::BuildSdfTextGlyphDraws({&request, 1}, b, nullptr, 128, &newLease));
    assert(!b.empty() && newLease && a.front().atlasTexture != b.front().atlasTexture);
    assert(oldLease.GetRetainedGenerationCount() == 1);
    // 壊れた再インポートは既存の正常な世代を上書きしない。
    {
        std::ofstream file(copy, std::ios::binary | std::ios::trunc);
        file << "invalid";
    }
    request.fontRevision = 3;
    std::vector<TR::SdfTextGlyphDraw> invalid;
    TR::SdfTextLayoutStats invalidStats;
    assert(TR::BuildSdfTextGlyphDraws({&request, 1}, invalid, &invalidStats));
    assert(invalid.empty() && invalidStats.failedRequestCount == 1);
    request.fontRevision = 2;
    assert(TR::BuildSdfTextGlyphDraws({&request, 1}, invalid));
    assert(invalid.front().atlasTexture == b.front().atlasTexture);
}
void ClipInvalidatesCachedFrame(const std::filesystem::path& font, const std::filesystem::path& output) {
    const auto project = output / "clip-cache-project";
    const auto previewFont = project / "Assets/Fonts/Preview.otf";
    std::filesystem::create_directories(previewFont.parent_path());
    std::filesystem::copy_file(font, previewFont, std::filesystem::copy_options::overwrite_existing);

    GAME_UI::GameUiPresentationService sprites;
    sprites.SetProjectRoot(project);
    GameProjectUiSpriteRequest spriteRequest{};
    spriteRequest.sourceId = 1;
    GAME_UI::GameUiClipRect clip{true, 0, 0, 100, 50};
    assert(sprites.SubmitSprite("generated://clip-contract", spriteRequest, {}, clip));
    sprites.Tick(1);
    const uint64_t spriteRevision = sprites.GetFrameSource().revision;
    assert(spriteRevision != 0);
    sprites.DiscardPending();
    clip.left = 1;
    assert(sprites.SubmitSprite("generated://clip-contract", spriteRequest, {}, clip));
    assert(!sprites.PendingMatchesFrame());
    sprites.Tick(2);
    assert(sprites.GetFrameSource().revision > spriteRevision);

    GAME_UI::GameUiPresentationService texts;
    texts.SetProjectRoot(project);
    GameProjectUiTextRequest textRequest{};
    textRequest.sourceId = 2;
    clip.left = 0;
    assert(texts.SubmitStyledText("Fonts/Preview.otf", "clip contract", textRequest, {}, {}, clip));
    texts.Tick(3);
    const uint64_t textRevision = texts.GetFrameSource().revision;
    assert(textRevision != 0);
    texts.DiscardPending();
    clip.left = 1;
    assert(texts.SubmitStyledText("Fonts/Preview.otf", "clip contract", textRequest, {}, {}, clip));
    assert(!texts.PendingMatchesFrame());
    texts.Tick(4);
    assert(texts.GetFrameSource().revision > textRevision);
}
size_t Capture(TESTS::INTERNAL::GpuContractDevice& gpu, GFX::Context& graphics,
               const GAME_UI::GameUiFrameSource& source, const std::filesystem::path& file,
               uint32_t expectedFailedTexts = 0, const UiResolvedLayout* expectedBounds = nullptr) {
    constexpr UINT width = 1280, height = 720;
    EDITOR::GameUiPreviewTarget target;
    gpu.ResetCommands();
    ++graphics.currentFrameRetireFenceValue;
    graphics.frameIndex = graphics.currentFrameRetireFenceValue % GFX::kFrameResourceCount;
    GFX::PublishCurrentContext(graphics);
    const auto image = target.Render(source, width, height, {.018f, .026f, .043f, 1});
    assert(image.ptr && target.FailedTextRequests() == expectedFailedTexts);
    // ImGui に渡す実際の SRV を読み戻す。テスト専用の描画経路を作らない。
    ID3D12Resource* resource = nullptr;
    for (UINT handle = 0; handle < DXTEX::DxTextureManager::GetMaxDescriptorCount(); ++handle)
        if (DXTEX::DxTextureManager::IsTextureHandleValid(handle) &&
            DXTEX::DxTextureManager::GetSrvGpuHandle(handle).ptr == image.ptr) {
            resource = DXTEX::DxTextureManager::GetResource(handle);
            break;
        }
    assert(resource);
    const auto transition = CD3DX12_RESOURCE_BARRIER::Transition(resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.commands->ResourceBarrier(1, &transition);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    const auto desc = resource->GetDesc();
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    ComPtr<ID3D12Resource> readback;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))));
    CD3DX12_TEXTURE_COPY_LOCATION from(resource, 0), to(readback.Get(), footprint);
    gpu.commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    gpu.SubmitAndWait(graphics.currentFrameRetireFenceValue);
    uint8_t* pixels = nullptr;
    assert(SUCCEEDED(readback->Map(0, nullptr, reinterpret_cast<void**>(&pixels))));
    size_t bright = 0;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const auto* p = pixels + footprint.Offset + y * footprint.Footprint.RowPitch + x * 4;
            if (p[0] > 150 && p[1] > 150 && p[2] > 150) {
                ++bright;
                if (expectedBounds)
                    assert(ContainsUiPoint(*expectedBounds, x + .5f, y + .5f));
            }
        }
    assert(bright > 200); // 実際に glyph が出力され、空のクリア画像ではない。
    DirectX::Image pixelsImage{width,
                               height,
                               DXGI_FORMAT_R8G8B8A8_UNORM,
                               footprint.Footprint.RowPitch,
                               static_cast<size_t>(bytes),
                               pixels + footprint.Offset};
    assert(
        SUCCEEDED(DirectX::SaveToWICFile(pixelsImage, DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, file.c_str())));
    readback->Unmap(0, nullptr);
    std::cout << "Rendered pixels verified: " << bright << " bright pixels\n";
    return bright;
}
void RuntimeLogicalToPhysicalScissor(TESTS::INTERNAL::GpuContractDevice& gpu, GFX::Context& graphics,
                                     const std::filesystem::path& output) {
    constexpr uint32_t logicalWidth = 1920u;
    constexpr uint32_t logicalHeight = 1080u;
    constexpr uint32_t physicalWidth = 2564u;
    constexpr uint32_t physicalHeight = 1570u;
    const auto texturePath = output / "scissor-contract-white.png";
    std::array<uint8_t, 4u> white{255u, 255u, 255u, 255u};
    DirectX::Image image{1u, 1u, DXGI_FORMAT_R8G8B8A8_UNORM, 4u, 4u, white.data()};
    assert(SUCCEEDED(
        DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, texturePath.c_str())));

    RenderTarget2D target;
    target.UpdateContext(graphics);
    assert(target.Init(physicalWidth, physicalHeight, DXGI_FORMAT_R8G8B8A8_UNORM, false, {0, 0, 0, 0}));

    GAME_UI::GameUiFrameSource source;
    source.frameIndex = 17u;
    auto addSprite = [&](uint64_t sourceId, float referenceWidth, float referenceHeight, float x, float y, float width,
                         float height, GameProjectUiColor tint, GAME_UI::GameUiClipRect clip = {}) {
        GAME_UI::GameUiSpriteDraw draw{};
        draw.absoluteAssetPath = TEXT::PathToUtf8(texturePath);
        draw.request.sourceId = sourceId;
        draw.request.referenceWidth = referenceWidth;
        draw.request.referenceHeight = referenceHeight;
        draw.request.x = x;
        draw.request.y = y;
        draw.request.width = width;
        draw.request.height = height;
        draw.request.tint = tint;
        draw.clip = clip;
        source.sprites.push_back(std::move(draw));
    };

    // Unclipped lower-right content must reach the physical backbuffer area beyond 1920x1080.
    addSprite(1u, 1920.0f, 1080.0f, 1800.0f, 980.0f, 110.0f, 80.0f, {1, 0, 0, 1});
    // The clipped region is checked in physical pixel coordinates, without reusing the renderer's resolver.
    addSprite(2u, 1920.0f, 1080.0f, 1200.0f, 700.0f, 700.0f, 300.0f, {0, 1, 0, 1},
              {true, 1400.0f, 750.0f, 1700.0f, 950.0f});
    // A narrower reference canvas creates a known horizontal CanvasFit offset.
    addSprite(3u, 1280.0f, 800.0f, 0.0f, 0.0f, 1280.0f, 800.0f, {0, 0, 1, 1}, {true, 800.0f, 50.0f, 1150.0f, 150.0f});
    addSprite(4u, 1920.0f, 1080.0f, 300.0f, 300.0f, 200.0f, 120.0f, {1, 1, 0, 1},
              {true, 450.0f, 350.0f, 450.0f, 420.0f});
    addSprite(5u, 1920.0f, 1080.0f, 800.0f, 300.0f, 200.0f, 120.0f, {1, 0, 1, 1},
              {true, 950.0f, 350.0f, 900.0f, 420.0f});

    ++graphics.currentFrameRetireFenceValue;
    graphics.frameIndex = graphics.currentFrameRetireFenceValue % GFX::kFrameResourceCount;
    GFX::PublishCurrentContext(graphics);
    gpu.ResetCommands();
    target.BeginCapture(0, 0, 0, 0);
    GAME_UI::BeginGameUiFrame();
    GAME_UI::SetGameUiFrameSource(&source);
    assert(GAME_UI::RenderGameUiFrame(source.frameIndex, logicalWidth, logicalHeight, physicalWidth, physicalHeight));
    target.EndCapture();
    target.TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    const auto targetDesc = target.GetResource()->GetDesc();
    gpu.device->GetCopyableFootprints(&targetDesc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    const auto readbackHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto readbackBuffer = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    ComPtr<ID3D12Resource> readback;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackBuffer,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&readback))));
    CD3DX12_TEXTURE_COPY_LOCATION from(target.GetResource(), 0), to(readback.Get(), footprint);
    graphics.cmdList->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    target.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.SubmitAndWait(graphics.currentFrameRetireFenceValue);
    uint8_t* mapped = nullptr;
    assert(SUCCEEDED(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped))));
    const auto pixel = [&](uint32_t x, uint32_t y) {
        const size_t index = footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch + x * 4u;
        return std::array<uint8_t, 4u>{mapped[index], mapped[index + 1u], mapped[index + 2u], mapped[index + 3u]};
    };
    const auto isColor = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
        const auto value = pixel(x, y);
        return value[3] > 200u && std::abs(static_cast<int>(value[0]) - r) < 16 &&
               std::abs(static_cast<int>(value[1]) - g) < 16 && std::abs(static_cast<int>(value[2]) - b) < 16;
    };
    const auto isClear = [&](uint32_t x, uint32_t y) { return pixel(x, y)[3] < 16u; };
    assert(isColor(2480u, 1480u, 255u, 0u, 0u));
    assert(isColor(2069u, 1236u, 0u, 255u, 0u));
    assert(isClear(1800u, 1236u) && isClear(2350u, 1236u) && isClear(2069u, 1050u) && isClear(2069u, 1450u));
    assert(isColor(1850u, 200u, 0u, 0u, 255u));
    assert(isClear(1500u, 200u) && isClear(2300u, 200u) && isClear(1850u, 50u) && isClear(1850u, 350u));
    assert(isClear(600u, 550u) && isClear(1230u, 550u));
    readback->Unmap(0, nullptr);
    // 起動時に古い 16:9 の参照寸法しかなくても、黒と UI alpha は描画先の四隅まで届く。
    source.sprites.resize(1);
    auto& cover = source.sprites.front();
    cover.absoluteAssetPath = "generated://solid/ffffffff";
    cover.request.flags = kGameProjectUiFillViewport | kGameProjectUiSubmissionModal;
    cover.request.tint = {0, 0, 0, 1};
    ++source.frameIndex;
    ++graphics.currentFrameRetireFenceValue;
    graphics.frameIndex = graphics.currentFrameRetireFenceValue % GFX::kFrameResourceCount;
    GFX::PublishCurrentContext(graphics);
    gpu.ResetCommands();
    target.BeginCapture(1, 0, 1, 0);
    GAME_UI::BeginGameUiFrame();
    GAME_UI::SetGameUiFrameSource(&source);
    assert(GAME_UI::RenderGameUiFrame(source.frameIndex, logicalWidth, logicalHeight, physicalWidth, physicalHeight));
    target.EndCapture();
    target.TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
    graphics.cmdList->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    target.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.SubmitAndWait(graphics.currentFrameRetireFenceValue);
    assert(SUCCEEDED(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped))));
    for (uint32_t y : {0u, physicalHeight / 2u, physicalHeight - 1u})
        for (uint32_t x : {0u, physicalWidth / 2u, physicalWidth - 1u})
            assert(isColor(x, y, 0u, 0u, 0u));
    readback->Unmap(0, nullptr);
    GAME_UI::BeginGameUiFrame();
    std::cout << "Runtime UI logical-to-physical scissor contract passed.\n";
}
void AnimatedTextPreview(TESTS::INTERNAL::GpuContractDevice& gpu, GFX::Context& graphics,
                         const std::filesystem::path& font, const std::filesystem::path& output) {
    const auto project = output / "animated-canvas";
    std::filesystem::create_directories(project / "Assets/Fonts");
    std::filesystem::copy_file(font, project / "Assets/Fonts/Preview.otf",
                               std::filesystem::copy_options::overwrite_existing);
    World world;
    auto* canvas = world.CreateObject("Canvas");
    canvas->SetDocumentId({1});
    canvas->AddComponent<UiCanvasComponent>()->Deserialize({{"referenceWidth", 1280}, {"referenceHeight", 720}});
    auto* parent = world.CreateObject("Panel");
    parent->SetDocumentId({2});
    parent->SetParent(canvas);
    parent->AddComponent<UiRectComponent>()->Deserialize({{"anchorMinimum", {0, 0}},
                                                          {"anchorMaximum", {0, 0}},
                                                          {"offsetMinimum", {300, 220}},
                                                          {"offsetMaximum", {900, 500}},
                                                          {"rotationDegrees", 25}});
    auto* child = world.CreateObject("Caption");
    child->SetDocumentId({3});
    child->SetParent(parent);
    child->AddComponent<UiRectComponent>()->Deserialize({{"anchorMinimum", {0, 0}},
                                                         {"anchorMaximum", {0, 0}},
                                                         {"offsetMinimum", {120, 80}},
                                                         {"offsetMaximum", {480, 200}},
                                                         {"rotationDegrees", -35}});
    child->AddComponent<UiTextComponent>()->Deserialize({{"fontAssetPath", "Fonts/Preview.otf"},
                                                         {"text", "Motion 012"},
                                                         {"pixelHeight", 48},
                                                         {"color", {1, 1, 1, 1}},
                                                         {"outlineWidth", 0}});
    UiSceneSystem system;
    auto asset = std::make_shared<UiAnimationAsset>();
    UiAnimationClip clip;
    clip.name = "Stretch";
    clip.duration = 1;
    clip.tracks = {{1, "self", UiMotionProperty::ScaleX, {1, {{0, 1, 0, 0}, {1, 1.4f, 0, 0}}}},
                   {2, "self", UiMotionProperty::ScaleY, {1, {{0, 1, 0, 0}, {1, .6f, 0, 0}}}}};
    asset->clips.push_back(clip);
    assert(system.Animations().Play(world, *parent, asset, "Stretch"));
    assert(system.Animations().Seek(world, parent->GetRuntimeHandle(), .8f));
    GAME_UI::GameUiPresentationService presentation;
    presentation.SetProjectRoot(project);
    FrameContext frame;
    frame.frameIndex = 1;
    system.BuildPresentation(world, frame, presentation);
    presentation.Tick(1);
    const auto bounds = system.ResolveLayout(world, *child);
    assert(bounds.valid && presentation.GetFrameSource().texts.size() == 1);
    Capture(gpu, graphics, presentation.GetFrameSource(), output / "animated-text-transform.png", 0, &bounds);
}
void WorkspaceTextPreview(TESTS::INTERNAL::GpuContractDevice& gpu, GFX::Context& graphics,
                          const std::filesystem::path& font, const std::filesystem::path& output) {
    const auto root = output / "workspace";
    std::filesystem::create_directories(root / "Assets/Fonts");
    const auto copy = root / "Assets/Fonts/Preview.otf";
    std::filesystem::copy_file(font, copy, std::filesystem::copy_options::overwrite_existing);
    AssetDatabase database;
    assert(database.Initialize(root) && database.ScanAssets(true));
    const auto guid = database.FindByPath(copy)->guid;
    assert(database.ImportAsset(guid));
    EDITOR::TextStyleWorkspaceController workspace;
    std::string error;
    assert(workspace.Document().New("Preview", error));
    assert(workspace.BuildPreview(database).texts.empty());
    assert(std::string_view(workspace.PreviewMessageKey()) == "uix.fontMissing");
    auto style = workspace.Document().Profile();
    style.font = guid;
    assert(workspace.Document().Apply(style, error));
    auto source = workspace.BuildPreview(database);
    assert(!workspace.PreviewMessageKey() && source.texts.size() == 1);
    assert(source.texts.front().request.sourceId != 0);
    const auto bright = Capture(gpu, graphics, source, output / "text-style-workspace.png");
    style.pixelHeight = 72;
    assert(workspace.Document().Apply(style, error));
    assert(workspace.BuildPreview(database).texts.front().request.pixelHeight == 72);
    assert(workspace.Document().Undo());
    workspace.Deactivate();
    source = workspace.BuildPreview(database);
    assert(Capture(gpu, graphics, source, output / "text-style-reopened.png") == bright);
    auto missing = source.texts.front();
    missing.absoluteFontAssetPath = TEXT::PathToUtf8(root / "missing.ttf");
    source.texts.push_back(missing);
    assert(Capture(gpu, graphics, source, output / "text-style-partial-font-failure.png", 1) == bright);
}

// HIKARI_TEST_FONT が無ければ Windows 標準の日本語フォントを使う。どれも無い環境だけ skip する。
std::string ResolveTestFont() {
    if (const char* font = std::getenv("HIKARI_TEST_FONT"))
        return font;
    wchar_t windows[MAX_PATH]{};
    if (GetWindowsDirectoryW(windows, MAX_PATH) == 0)
        return {};
    for (const wchar_t* name : {L"NotoSansJP-VF.ttf", L"YuGothR.ttc", L"meiryo.ttc", L"msgothic.ttc"}) {
        const std::filesystem::path path = std::filesystem::path(windows) / L"Fonts" / name;
        if (std::filesystem::exists(path))
            return TEXT::PathToUtf8(path);
    }
    return {};
}
} // namespace
int main(int argc, char** argv) {
    const std::string font = ResolveTestFont();
    if (font.empty() && argc != 4)
        return 77;
    const auto output = TEXT::PathFromUtf8(argv[0]).parent_path() / "ui-rendering";
    std::filesystem::create_directories(output);
    assert(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    assert(CONTENT::GetEngineContentContext().Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        debug->EnableDebugLayer();
    ComPtr<ID3D12Device> hardware;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&hardware))))
        return 77;
    TESTS::INTERNAL::GpuContractDevice gpu(hardware.Get());
    ComPtr<ID3D12InfoQueue> diagnostics;
    gpu.device.As(&diagnostics);
    GFX::Context graphics{};
    graphics.device = gpu.device.Get();
    graphics.cmdList = gpu.commands.Get();
    graphics.queue = gpu.queue.Get();
    graphics.deviceGeneration = 1;
    GFX::PublishCurrentContext(graphics);
    assert(DXTEX::DxTextureManager::Init(graphics));
    GAME_UI::GameUiFrameSource source;
    if (argc == 4)
        source = LoadCanvas(TEXT::PathFromUtf8(argv[1]), TEXT::PathFromUtf8(argv[2]), output);
    else {
        source.frameIndex = 1;
        GAME_UI::GameUiTextDraw text;
        text.absoluteFontAssetPath = font;
        text.fontRevision = 1;
        text.utf8Text = "UI Text 文字スタイル";
        text.request.x = 100;
        text.request.y = 100;
        text.request.pixelHeight = 64;
        text.request.sourceId = 1;
        text.request.maxWidth = 1000;
        text.request.maxHeight = 120;
        source.texts.push_back(text);
    }
    const auto fontPath = TEXT::PathFromUtf8(source.texts.front().absoluteFontAssetPath);
    CheckAtlasReload(fontPath, output);
    ClipInvalidatesCachedFrame(fontPath, output);
    Capture(gpu, graphics, source, argc == 4 ? TEXT::PathFromUtf8(argv[3]) : output / "ui.png");
    if (argc != 4) {
        auto& text = source.texts.front();
        text.editing.feedback = std::make_shared<GAME_UI::GameUiTextLayoutFeedback>();
        text.editing.feedback->expected = GAME_UI::MakeGameUiTextLayoutRequest(text);
        text.editing.focused = true;
        text.editing.caret = 2;
        text.editing.selectionEnd = 2;
        source.revision++;
        Capture(gpu, graphics, source, output / "ui-editing.png");
        assert(text.editing.feedback->ready && !text.editing.feedback->stops.empty());
    }
    WorkspaceTextPreview(gpu, graphics, fontPath, output);
    AnimatedTextPreview(gpu, graphics, fontPath, output);
    RuntimeLogicalToPhysicalScissor(gpu, graphics, output);
    if (diagnostics) {
        for (UINT64 i = 0; i < diagnostics->GetNumStoredMessages(); ++i) {
            SIZE_T size = 0;
            diagnostics->GetMessage(i, nullptr, &size);
            std::vector<uint8_t> bytes(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            assert(SUCCEEDED(diagnostics->GetMessage(i, message, &size)));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                std::cerr << message->pDescription << '\n';
                assert(false);
            }
        }
    }
    GAME_UI::ShutdownGameUiRenderer();
    std::cout << "UI rendering and font-generation contracts passed.\n";
}
