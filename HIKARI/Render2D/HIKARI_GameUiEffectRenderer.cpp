#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Render2D/HIKARI_GameUiEffectRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "Render2D/HIKARI_GameUiTextLayout.h"
#include "Render2D/HIKARI_SurfaceTextureTargetRing.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Core/HIKARI_TimeService.h"

namespace HIKARI::GAME_UI {
namespace {
using Microsoft::WRL::ComPtr;

constexpr uint32_t kVerticesPerSprite = 6u;
constexpr uint32_t kConstantStride = 256u;

struct UiVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

struct alignas(16) UiConstants {
    float screenSize[2]{};
    float texelSize[2]{};
    float tint[4]{};
    float effectColor[4]{};
    float highlightColor[4]{};
    float params0[4]{};
    float params1[4]{};
};

static_assert(sizeof(UiConstants) == 96u);

struct FrameResources {
    ComPtr<ID3D12Resource> vertices{};
    ComPtr<ID3D12Resource> constants{};
    UiVertex* vertexMapped = nullptr;
    uint8_t* constantMapped = nullptr;
    size_t spriteCapacity = 0u;
};

struct RendererState {
    const GameUiFrameSource* source = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipeline{};
    std::array<FrameResources, GFX::kFrameResourceCount> frames{};
    std::unordered_map<std::string, int> textureHandles{};
    ID3D12Device* deviceIdentity = nullptr;
    bool compileFailureLogged = false;
};

RendererState g{};
RendererState gPreview{};
RendererState gWorkspacePreview{};
struct CanvasRenderState {
    RendererState renderer;
    SurfaceTextureTargetRing target;
    RENDER3D::TextureResourceHandle published{};
    uint64_t revision{}, renderedFrame{}, renderedFence{};
};
std::array<CanvasRenderState, kGameProjectUiCanvasMaximumCount> gCanvases{};

struct ResolvedDraw {
    int textureHandle = -1;
    GameProjectUiSpriteRequest request{};
    Affine2D transform{};
    GameUiClipRect clip{};
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    uint64_t submissionSequence = 0u;
    uint32_t elementSequence = 0u;
};

using MATH::IsFinite;

void ReleaseFrame(FrameResources& frame) noexcept {
    if (frame.vertices != nullptr && frame.vertexMapped != nullptr) {
        frame.vertices->Unmap(0u, nullptr);
    }
    if (frame.constants != nullptr && frame.constantMapped != nullptr) {
        frame.constants->Unmap(0u, nullptr);
    }
    frame = {};
}

void ReleasePipeline(RendererState& state) noexcept {
    for (FrameResources& frame : state.frames) {
        ReleaseFrame(frame);
    }
    state.pipeline.Reset();
    state.rootSignature.Reset();
    state.textureHandles.clear();
    state.deviceIdentity = nullptr;
    state.compileFailureLogged = false;
}

bool EnsurePipeline(RendererState& state, ID3D12Device* device) {
    if (device == nullptr) {
        return false;
    }
    if (state.deviceIdentity != nullptr && state.deviceIdentity != device) {
        ReleasePipeline(state);
    }
    state.deviceIdentity = device;
    if (state.rootSignature != nullptr && state.pipeline != nullptr) {
        return true;
    }

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1u;
    range.BaseShaderRegister = 0u;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 2u> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0u;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0u;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(parameters.size());
    rootDesc.pParameters = parameters.data();
    rootDesc.NumStaticSamplers = 1u;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(device, rootDesc, state.rootSignature.GetAddressOf()),
                         "GameUi::CreateRootSignature")) {
        return false;
    }

    ComPtr<ID3DBlob> vertexShader{};
    ComPtr<ID3DBlob> pixelShader{};
    if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render2D_GameUiEffect.hlsl", "VSMain", GFX::ShaderStage::Vertex,
                                   vertexShader.GetAddressOf()) ||
        !GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render2D_GameUiEffect.hlsl", "PSMain", GFX::ShaderStage::Pixel,
                                   pixelShader.GetAddressOf())) {
        if (!state.compileFailureLogged) {
            DEBUGLOG::PushRenderError("[GameUi][ERROR] effect shader compile failed.");
            state.compileFailureLogged = true;
        }
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC input[] = {
        {"POSITION", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u, static_cast<UINT>(offsetof(UiVertex, x)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u},
        {"TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u, static_cast<UINT>(offsetof(UiVertex, u)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = state.rootSignature.Get();
    desc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    desc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    auto& blend = desc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.InputLayout = {input, static_cast<UINT>(std::size(input))};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1u;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1u;
    const bool created =
        HIKARI_DX_CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(state.pipeline.GetAddressOf())),
                        "GameUi::CreateEffectPipeline");
    if (created) {
        state.compileFailureLogged = false;
    }
    return created;
}

bool EnsureFrameResources(ID3D12Device* device, FrameResources& frame, size_t spriteCount) {
    if (device == nullptr || spriteCount == 0u) {
        return false;
    }
    if (frame.vertices != nullptr && frame.constants != nullptr && frame.vertexMapped != nullptr &&
        frame.constantMapped != nullptr && frame.spriteCapacity >= spriteCount) {
        return true;
    }

    ReleaseFrame(frame);
    frame.spriteCapacity = (std::max)(spriteCount, size_t{64u});
    const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(frame.spriteCapacity * kVerticesPerSprite * sizeof(UiVertex));
    const auto constantDesc = CD3DX12_RESOURCE_DESC::Buffer(frame.spriteCapacity * kConstantStride);
    if (!HIKARI_DX_CHECK(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                         IID_PPV_ARGS(frame.vertices.GetAddressOf())),
                         "GameUi::CreateVertexBuffer") ||
        !HIKARI_DX_CHECK(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &constantDesc,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                         IID_PPV_ARGS(frame.constants.GetAddressOf())),
                         "GameUi::CreateConstantBuffer") ||
        !HIKARI_DX_CHECK(frame.vertices->Map(0u, nullptr, reinterpret_cast<void**>(&frame.vertexMapped)),
                         "GameUi::MapVertexBuffer") ||
        !HIKARI_DX_CHECK(frame.constants->Map(0u, nullptr, reinterpret_cast<void**>(&frame.constantMapped)),
                         "GameUi::MapConstantBuffer")) {
        ReleaseFrame(frame);
        return false;
    }
    return true;
}

int ResolveTexture(RendererState& state, const std::string& path) {
    auto found = state.textureHandles.find(path);
    if (found != state.textureHandles.end() && DXTEX::DxTextureManager::IsTextureHandleValid(found->second)) {
        return found->second;
    }
    const std::string name = "GameUi:" + path;
    const int handle = DXTEX::DxTextureManager::LoadTextureSrgb(name, path);
    if (handle >= 0) {
        state.textureHandles[path] = handle;
    }
    return handle;
}

void FillQuad(UiVertex* destination, const GameProjectUiSpriteRequest& request, const Affine2D& transform,
              int logicalWidth, int logicalHeight, float u0, float v0, float u1, float v1) noexcept {
    GameProjectUiCanvasFit fit{};
    if (!ResolveGameProjectUiCanvasFit(static_cast<uint32_t>(logicalWidth), static_cast<uint32_t>(logicalHeight),
                                       request.referenceWidth, request.referenceHeight, fit)) {
        return;
    }
    const float x = request.x;
    const float y = request.y;
    const float width = request.width;
    const float height = request.height;
    const float pivotX = x + width * request.pivotX;
    const float pivotY = y + height * request.pivotY;
    const float cosine = std::cos(request.rotationRadians);
    const float sine = std::sin(request.rotationRadians);
    auto rotate = [&](float px, float py) noexcept {
        const float localX = px - pivotX;
        const float localY = py - pivotY;
        px = pivotX + localX * cosine - localY * sine;
        py = pivotY + localX * sine + localY * cosine;
        transform.Transform(px, py);
        return std::array<float, 2u>{fit.offsetX + px * fit.scale, fit.offsetY + py * fit.scale};
    };
    const auto lt = rotate(x, y);
    const auto rt = rotate(x + width, y);
    const auto lb = rotate(x, y + height);
    const auto rb = rotate(x + width, y + height);
    const UiVertex vertices[kVerticesPerSprite] = {
        {lt[0], lt[1], u0, v0}, {rt[0], rt[1], u1, v0}, {lb[0], lb[1], u0, v1},
        {lb[0], lb[1], u0, v1}, {rt[0], rt[1], u1, v0}, {rb[0], rb[1], u1, v1},
    };
    std::memcpy(destination, vertices, sizeof(vertices));
}

void CopyColor(float* destination, const GameProjectUiColor& color) {
    destination[0] = color.r;
    destination[1] = color.g;
    destination[2] = color.b;
    destination[3] = color.a;
}

bool ResolveScissor(const GameUiClipRect& clip, const GameProjectUiSpriteRequest& request, int logicalWidth,
                    int logicalHeight, D3D12_RECT& outRect) noexcept {
    outRect = {0, 0, logicalWidth, logicalHeight};
    if (!clip.enabled)
        return true;
    if (!IsFinite(clip.left) || !IsFinite(clip.top) || !IsFinite(clip.right) || !IsFinite(clip.bottom))
        return false;
    GameProjectUiCanvasFit fit{};
    if (!ResolveGameProjectUiCanvasFit(static_cast<uint32_t>(logicalWidth), static_cast<uint32_t>(logicalHeight),
                                       request.referenceWidth, request.referenceHeight, fit))
        return false;
    const float left = std::clamp(fit.offsetX + clip.left * fit.scale, 0.0f, static_cast<float>(logicalWidth));
    const float top = std::clamp(fit.offsetY + clip.top * fit.scale, 0.0f, static_cast<float>(logicalHeight));
    const float right = std::clamp(fit.offsetX + clip.right * fit.scale, 0.0f, static_cast<float>(logicalWidth));
    const float bottom = std::clamp(fit.offsetY + clip.bottom * fit.scale, 0.0f, static_cast<float>(logicalHeight));
    outRect.left = static_cast<LONG>(std::floor(left));
    outRect.top = static_cast<LONG>(std::floor(top));
    outRect.right = static_cast<LONG>(std::ceil(right));
    outRect.bottom = static_cast<LONG>(std::ceil(bottom));
    return outRect.right > outRect.left && outRect.bottom > outRect.top;
}
} // namespace

void BeginGameUiFrame() noexcept { g.source = nullptr; }

void SetGameUiFrameSource(const GameUiFrameSource* source) noexcept { g.source = source; }

static bool RenderGameUiSource(RendererState& state, const GameUiFrameSource* source, uint64_t frameIndex,
                               int logicalWidth, int logicalHeight, uint32_t* failedTextRequests = nullptr) noexcept {
    if (failedTextRequests)
        *failedTextRequests = 0;
    try {
        if (source == nullptr || !source->HasDraws() || source->frameIndex != frameIndex) {
            return true;
        }
        const GFX::Context& graphics = GFX::GetCurrentContext();
        if (graphics.device == nullptr || graphics.cmdList == nullptr || logicalWidth <= 0 || logicalHeight <= 0 ||
            !EnsurePipeline(state, graphics.device)) {
            return false;
        }

        std::vector<GameUiTextGlyphDraw> textGlyphs{};
        TEXT_RENDERING::SdfTextLayoutLease textAtlasLease{};
        if (!BuildGameUiTextGlyphDraws(source->texts, textGlyphs, &textAtlasLease, failedTextRequests)) {
            return false;
        }
        std::vector<ResolvedDraw> resolved{};
        resolved.reserve(source->sprites.size() + textGlyphs.size());
        for (const GameUiSpriteDraw& draw : source->sprites) {
            const int texture = ResolveTexture(state, draw.absoluteAssetPath);
            if (texture < 0) {
                continue;
            }
            ResolvedDraw item{};
            item.textureHandle = texture;
            item.request = draw.request;
            item.transform = draw.transform;
            item.clip = draw.clip;
            item.submissionSequence = draw.submissionSequence;
            resolved.push_back(std::move(item));
        }
        for (const GameUiTextGlyphDraw& glyph : textGlyphs) {
            if (glyph.textureHandle < 0) {
                continue;
            }
            ResolvedDraw item{};
            item.textureHandle = glyph.textureHandle;
            item.request = glyph.request;
            item.transform = glyph.transform;
            item.clip = glyph.clip;
            item.u0 = glyph.u0;
            item.v0 = glyph.v0;
            item.u1 = glyph.u1;
            item.v1 = glyph.v1;
            item.submissionSequence = glyph.submissionSequence;
            item.elementSequence = glyph.glyphSequence;
            resolved.push_back(std::move(item));
        }
        std::stable_sort(resolved.begin(), resolved.end(), [](const ResolvedDraw& lhs, const ResolvedDraw& rhs) {
            if (lhs.request.order != rhs.request.order) {
                return lhs.request.order < rhs.request.order;
            }
            if (lhs.request.sourceId != rhs.request.sourceId) {
                return lhs.request.sourceId < rhs.request.sourceId;
            }
            if (lhs.submissionSequence != rhs.submissionSequence) {
                return lhs.submissionSequence < rhs.submissionSequence;
            }
            return lhs.elementSequence < rhs.elementSequence;
        });
        if (resolved.empty()) {
            return true;
        }

        FrameResources& frame = state.frames[graphics.frameIndex % GFX::kFrameResourceCount];
        if (!EnsureFrameResources(graphics.device, frame, resolved.size())) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = graphics.cmdList;
        ID3D12DescriptorHeap* textureHeap = DXTEX::DxTextureManager::GetSrvHeap();
        if (textureHeap == nullptr) {
            return false;
        }
        GFX::PIX::ScopedGpuEvent event(cmd, GFX::PIX::kColorEditor, "Game UI Shader Effects");
        cmd->SetDescriptorHeaps(1u, &textureHeap);
        cmd->SetGraphicsRootSignature(state.rootSignature.Get());
        cmd->SetPipelineState(state.pipeline.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        size_t drawIndex = 0u;
        for (const ResolvedDraw& draw : resolved) {
            const int texture = draw.textureHandle;
            if (texture < 0) {
                continue;
            }
            UINT textureWidth = 0u;
            UINT textureHeight = 0u;
            DXTEX::DxTextureManager::GetTextureSize(texture, textureWidth, textureHeight);
            if (textureWidth == 0u || textureHeight == 0u) {
                continue;
            }
            D3D12_RECT scissor{};
            if (!ResolveScissor(draw.clip, draw.request, logicalWidth, logicalHeight, scissor))
                continue;
            cmd->RSSetScissorRects(1u, &scissor);

            UiVertex* vertices = frame.vertexMapped + drawIndex * kVerticesPerSprite;
            auto geometry = draw.request;
            if ((geometry.flags & kGameProjectUiPreserveSpriteAspect) != 0) {
                const float width =
                    geometry.height * static_cast<float>(textureWidth) / static_cast<float>(textureHeight);
                geometry.x += (geometry.width - width) * geometry.pivotX;
                geometry.width = width;
            }
            FillQuad(vertices, geometry, draw.transform, logicalWidth, logicalHeight, draw.u0, draw.v0, draw.u1,
                     draw.v1);
            UiConstants constants{};
            constants.screenSize[0] = static_cast<float>(logicalWidth);
            constants.screenSize[1] = static_cast<float>(logicalHeight);
            constants.texelSize[0] = 1.0f / static_cast<float>(textureWidth);
            constants.texelSize[1] = 1.0f / static_cast<float>(textureHeight);
            CopyColor(constants.tint, draw.request.tint);
            CopyColor(constants.effectColor, draw.request.effectColor);
            CopyColor(constants.highlightColor, draw.request.highlightColor);
            constants.params0[0] = draw.request.fillAmount;
            constants.params0[1] = draw.request.effectProgress;
            constants.params0[2] = draw.request.effectIntensity;
            constants.params0[3] = draw.request.timeSeconds;
            constants.params1[0] = draw.request.directionX;
            constants.params1[1] = draw.request.directionY;
            constants.params1[2] = static_cast<float>(static_cast<uint32_t>(draw.request.effect));
            constants.params1[3] = static_cast<float>(draw.request.flags);
            std::memcpy(frame.constantMapped + drawIndex * kConstantStride, &constants, sizeof(constants));

            const D3D12_VERTEX_BUFFER_VIEW vertexView{frame.vertices->GetGPUVirtualAddress() +
                                                          drawIndex * kVerticesPerSprite * sizeof(UiVertex),
                                                      kVerticesPerSprite * sizeof(UiVertex), sizeof(UiVertex)};
            cmd->IASetVertexBuffers(0u, 1u, &vertexView);
            cmd->SetGraphicsRootConstantBufferView(0u, frame.constants->GetGPUVirtualAddress() +
                                                           drawIndex * kConstantStride);
            cmd->SetGraphicsRootDescriptorTable(1u, DXTEX::DxTextureManager::GetSrvGpuHandle(texture));
            cmd->DrawInstanced(kVerticesPerSprite, 1u, 0u, 0u);
            ++drawIndex;
        }
        const D3D12_RECT fullScissor{0, 0, logicalWidth, logicalHeight};
        cmd->RSSetScissorRects(1u, &fullScissor);
        return true;
    } catch (...) {
        return false;
    }
}

bool RenderGameUiFrame(uint64_t frameIndex, int logicalWidth, int logicalHeight) noexcept {
    return RenderGameUiSource(g, g.source, frameIndex, logicalWidth, logicalHeight);
}

bool RenderGameUiPreviewFrame(const GameUiFrameSource& source, uint64_t frameIndex, int logicalWidth,
                              int logicalHeight) noexcept {
    return RenderGameUiSource(gPreview, &source, frameIndex, logicalWidth, logicalHeight);
}

bool RenderGameUiCanvasTarget(uint32_t slot, uint64_t worldIdentity, const GameUiFrameSource& source, uint64_t revision,
                              uint32_t width, uint32_t height, GameProjectUiColor clearColor,
                              RENDER3D::TextureResourceHandle& output, uint64_t& renderedRevision,
                              uint64_t& renderedFrame) noexcept {
    output = {};
    renderedRevision = 0;
    renderedFrame = 0;
    try {
        if (slot >= gCanvases.size() || !revision)
            return false;
        auto& state = gCanvases[slot];
        const auto& graphics = GFX::GetCurrentContext();
        if (state.target.worldIdentity != worldIdentity || state.target.deviceGeneration != graphics.deviceGeneration ||
            state.target.width != width || state.target.height != height) {
            state.published = {};
            state.revision = 0;
            state.renderedFrame = 0;
            state.renderedFence = 0;
        }
        if (!state.target.Ensure(graphics, worldIdentity, slot + 4, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, false,
                                 {clearColor.r, clearColor.g, clearColor.b, clearColor.a}))
            return false;
        if (state.revision != revision &&
            (!state.published || state.renderedFence != graphics.currentFrameRetireFenceValue)) {
            auto& target = *state.target.targets[state.target.writeIndex];
            target.BeginCapture(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
            const bool ok = RenderGameUiSource(state.renderer, &source, source.frameIndex, width, height);
            target.EndCapture();
            (void)POST::PostSystem::RebindCurrentRenderTarget();
            if (!ok)
                return false;
            state.published = state.target.textures[state.target.writeIndex];
            state.target.writeIndex = (state.target.writeIndex + 1) % GFX::kFrameResourceCount;
            state.revision = revision;
            state.renderedFrame = TIME::GetFrameContext().frameIndex;
            state.renderedFence = graphics.currentFrameRetireFenceValue;
        }
        output = state.published;
        renderedRevision = state.revision;
        renderedFrame = state.renderedFrame;
        return static_cast<bool>(output);
    } catch (...) {
        return false;
    }
}

void ResetGameUiCanvasTarget(uint32_t slot) noexcept {
    if (slot >= gCanvases.size())
        return;
    auto& state = gCanvases[slot];
    state.target.Reset();
    state.published = {};
    state.revision = 0;
    state.renderedFrame = 0;
    state.renderedFence = 0;
}

bool RenderGameUiWorkspacePreviewFrame(const GameUiFrameSource& source, int width, int height,
                                       uint32_t* failedTextRequests) noexcept {
    return RenderGameUiSource(gWorkspacePreview, &source, source.frameIndex, width, height, failedTextRequests);
}
bool GetGameUiBackdropRequest(uint64_t frameIndex, GameProjectUiBackdropRequest& outRequest) noexcept {
    outRequest = {};
    const GameUiFrameSource* source = g.source;
    if (source == nullptr || source->frameIndex != frameIndex || !source->hasBackdrop) {
        return false;
    }
    outRequest = source->backdrop.request;
    return true;
}

void ShutdownGameUiRenderer() noexcept {
    g.source = nullptr;
    gPreview.source = nullptr;
    ShutdownGameUiTextLayout();
    ReleasePipeline(g);
    ReleasePipeline(gPreview);
    ReleasePipeline(gWorkspacePreview);
    for (uint32_t i = 0; i < gCanvases.size(); ++i) {
        ResetGameUiCanvasTarget(i);
        ReleasePipeline(gCanvases[i].renderer);
    }
}

} // namespace HIKARI::GAME_UI
