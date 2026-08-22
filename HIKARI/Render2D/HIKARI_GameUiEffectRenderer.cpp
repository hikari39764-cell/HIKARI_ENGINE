#include "Render2D/HIKARI_GameUiEffectRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render2D/HIKARI_DxTexture.h"

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

        bool IsFinite(float value) noexcept {
            return std::isfinite(value);
        }

        void ReleaseFrame(FrameResources& frame) noexcept {
            if (frame.vertices != nullptr && frame.vertexMapped != nullptr) {
                frame.vertices->Unmap(0u, nullptr);
            }
            if (frame.constants != nullptr &&
                frame.constantMapped != nullptr) {
                frame.constants->Unmap(0u, nullptr);
            }
            frame = {};
        }

        void ReleasePipeline() noexcept {
            for (FrameResources& frame : g.frames) {
                ReleaseFrame(frame);
            }
            g.pipeline.Reset();
            g.rootSignature.Reset();
            g.textureHandles.clear();
            g.deviceIdentity = nullptr;
            g.compileFailureLogged = false;
        }

        bool EnsurePipeline(ID3D12Device* device) {
            if (device == nullptr) {
                return false;
            }
            if (g.deviceIdentity != nullptr &&
                g.deviceIdentity != device) {
                ReleasePipeline();
            }
            g.deviceIdentity = device;
            if (g.rootSignature != nullptr && g.pipeline != nullptr) {
                return true;
            }

            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1u;
            range.BaseShaderRegister = 0u;
            range.OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            std::array<D3D12_ROOT_PARAMETER, 2u> parameters{};
            parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[0].Descriptor.ShaderRegister = 0u;
            parameters[0].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            parameters[1].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[1].DescriptorTable.NumDescriptorRanges = 1u;
            parameters[1].DescriptorTable.pDescriptorRanges = &range;
            parameters[1].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ShaderRegister = 0u;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;

            D3D12_ROOT_SIGNATURE_DESC rootDesc{};
            rootDesc.NumParameters =
                static_cast<UINT>(parameters.size());
            rootDesc.pParameters = parameters.data();
            rootDesc.NumStaticSamplers = 1u;
            rootDesc.pStaticSamplers = &sampler;
            rootDesc.Flags =
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> signature{};
            ComPtr<ID3DBlob> errors{};
            if (!HIKARI_DX_CHECK(
                    D3D12SerializeRootSignature(
                        &rootDesc,
                        D3D_ROOT_SIGNATURE_VERSION_1,
                        signature.GetAddressOf(),
                        errors.GetAddressOf()),
                    "GameUi::SerializeRootSignature") ||
                !HIKARI_DX_CHECK(
                    device->CreateRootSignature(
                        0u,
                        signature->GetBufferPointer(),
                        signature->GetBufferSize(),
                        IID_PPV_ARGS(g.rootSignature.GetAddressOf())),
                    "GameUi::CreateRootSignature")) {
                return false;
            }

            ComPtr<ID3DBlob> vertexShader{};
            ComPtr<ID3DBlob> pixelShader{};
            if (!GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Render2D_GameUiEffect.hlsl",
                    "VSMain",
                    GFX::ShaderStage::Vertex,
                    vertexShader.GetAddressOf()) ||
                !GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Render2D_GameUiEffect.hlsl",
                    "PSMain",
                    GFX::ShaderStage::Pixel,
                    pixelShader.GetAddressOf())) {
                if (!g.compileFailureLogged) {
                    DEBUGLOG::PushRenderError(
                        "[GameUi][ERROR] effect shader compile failed.");
                    g.compileFailureLogged = true;
                }
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC input[] = {
                { "POSITION", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u,
                    static_cast<UINT>(offsetof(UiVertex, x)),
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
                { "TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u,
                    static_cast<UINT>(offsetof(UiVertex, u)),
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            };
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = g.rootSignature.Get();
            desc.VS = {
                vertexShader->GetBufferPointer(),
                vertexShader->GetBufferSize() };
            desc.PS = {
                pixelShader->GetBufferPointer(),
                pixelShader->GetBufferSize() };
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
            desc.RasterizerState =
                CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.DepthStencilState =
                CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.StencilEnable = FALSE;
            desc.InputLayout = {
                input,
                static_cast<UINT>(std::size(input)) };
            desc.PrimitiveTopologyType =
                D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1u;
            desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1u;
            const bool created = HIKARI_DX_CHECK(
                device->CreateGraphicsPipelineState(
                    &desc,
                    IID_PPV_ARGS(g.pipeline.GetAddressOf())),
                "GameUi::CreateEffectPipeline");
            if (created) {
                g.compileFailureLogged = false;
            }
            return created;
        }

        bool EnsureFrameResources(
            ID3D12Device* device,
            FrameResources& frame,
            size_t spriteCount) {
            if (device == nullptr || spriteCount == 0u) {
                return false;
            }
            if (frame.vertices != nullptr &&
                frame.constants != nullptr &&
                frame.vertexMapped != nullptr &&
                frame.constantMapped != nullptr &&
                frame.spriteCapacity >= spriteCount) {
                return true;
            }

            ReleaseFrame(frame);
            frame.spriteCapacity = (std::max)(spriteCount, size_t{ 64u });
            const auto uploadHeap =
                CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(
                frame.spriteCapacity * kVerticesPerSprite *
                    sizeof(UiVertex));
            const auto constantDesc = CD3DX12_RESOURCE_DESC::Buffer(
                frame.spriteCapacity * kConstantStride);
            if (!HIKARI_DX_CHECK(
                    device->CreateCommittedResource(
                        &uploadHeap,
                        D3D12_HEAP_FLAG_NONE,
                        &vertexDesc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(frame.vertices.GetAddressOf())),
                    "GameUi::CreateVertexBuffer") ||
                !HIKARI_DX_CHECK(
                    device->CreateCommittedResource(
                        &uploadHeap,
                        D3D12_HEAP_FLAG_NONE,
                        &constantDesc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(frame.constants.GetAddressOf())),
                    "GameUi::CreateConstantBuffer") ||
                !HIKARI_DX_CHECK(
                    frame.vertices->Map(
                        0u,
                        nullptr,
                        reinterpret_cast<void**>(&frame.vertexMapped)),
                    "GameUi::MapVertexBuffer") ||
                !HIKARI_DX_CHECK(
                    frame.constants->Map(
                        0u,
                        nullptr,
                        reinterpret_cast<void**>(&frame.constantMapped)),
                    "GameUi::MapConstantBuffer")) {
                ReleaseFrame(frame);
                return false;
            }
            return true;
        }

        int ResolveTexture(const std::string& path) {
            auto found = g.textureHandles.find(path);
            if (found != g.textureHandles.end() &&
                DXTEX::DxTextureManager::IsTextureHandleValid(
                    found->second)) {
                return found->second;
            }
            const std::string name = "GameUi:" + path;
            const int handle = DXTEX::DxTextureManager::LoadTextureSrgb(
                name,
                path);
            if (handle >= 0) {
                g.textureHandles[path] = handle;
            }
            return handle;
        }

        void FillQuad(
            UiVertex* destination,
            const GameProjectUiSpriteRequest& request,
            int logicalWidth,
            int logicalHeight) noexcept {
            const float scale = (std::min)(
                static_cast<float>(logicalWidth) /
                    request.referenceWidth,
                static_cast<float>(logicalHeight) /
                    request.referenceHeight);
            const float fittedWidth = request.referenceWidth * scale;
            const float fittedHeight = request.referenceHeight * scale;
            const float offsetX =
                (static_cast<float>(logicalWidth) - fittedWidth) * 0.5f;
            const float offsetY =
                (static_cast<float>(logicalHeight) - fittedHeight) * 0.5f;
            const float x = offsetX + request.x * scale;
            const float y = offsetY + request.y * scale;
            const float width = request.width * scale;
            const float height = request.height * scale;
            const float pivotX = x + width * request.pivotX;
            const float pivotY = y + height * request.pivotY;
            const float cosine = std::cos(request.rotationRadians);
            const float sine = std::sin(request.rotationRadians);
            auto rotate = [&](float px, float py) noexcept {
                const float localX = px - pivotX;
                const float localY = py - pivotY;
                return std::array<float, 2u>{
                    pivotX + localX * cosine - localY * sine,
                    pivotY + localX * sine + localY * cosine };
            };
            const auto lt = rotate(x, y);
            const auto rt = rotate(x + width, y);
            const auto lb = rotate(x, y + height);
            const auto rb = rotate(x + width, y + height);
            const UiVertex vertices[kVerticesPerSprite] = {
                { lt[0], lt[1], 0.0f, 0.0f },
                { rt[0], rt[1], 1.0f, 0.0f },
                { lb[0], lb[1], 0.0f, 1.0f },
                { lb[0], lb[1], 0.0f, 1.0f },
                { rt[0], rt[1], 1.0f, 0.0f },
                { rb[0], rb[1], 1.0f, 1.0f },
            };
            std::memcpy(
                destination,
                vertices,
                sizeof(vertices));
        }

        void CopyColor(float* destination, const GameProjectUiColor& color) {
            destination[0] = color.r;
            destination[1] = color.g;
            destination[2] = color.b;
            destination[3] = color.a;
        }
    }

    void BeginGameUiFrame() noexcept {
        g.source = nullptr;
    }

    void SetGameUiFrameSource(
        const GameUiFrameSource* source) noexcept {
        g.source = source;
    }

    bool RenderGameUiFrame(
        uint64_t frameIndex,
        int logicalWidth,
        int logicalHeight) noexcept {
        try {
            const GameUiFrameSource* source = g.source;
            if (source == nullptr || !source->HasSprites() ||
                source->frameIndex != frameIndex) {
                return true;
            }
            const GFX::Context& graphics = GFX::GetCurrentContext();
            if (graphics.device == nullptr || graphics.cmdList == nullptr ||
                logicalWidth <= 0 || logicalHeight <= 0 ||
                !EnsurePipeline(graphics.device)) {
                return false;
            }

            FrameResources& frame = g.frames[
                graphics.frameIndex % GFX::kFrameResourceCount];
            if (!EnsureFrameResources(
                    graphics.device,
                    frame,
                    source->sprites.size())) {
                return false;
            }

            ID3D12GraphicsCommandList* cmd = graphics.cmdList;
            ID3D12DescriptorHeap* textureHeap =
                DXTEX::DxTextureManager::GetSrvHeap();
            if (textureHeap == nullptr) {
                return false;
            }
            GFX::PIX::ScopedGpuEvent event(
                cmd,
                GFX::PIX::kColorEditor,
                "Game UI Shader Effects");
            cmd->SetDescriptorHeaps(1u, &textureHeap);
            cmd->SetGraphicsRootSignature(g.rootSignature.Get());
            cmd->SetPipelineState(g.pipeline.Get());
            cmd->IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            size_t drawIndex = 0u;
            for (const GameUiSpriteDraw& draw : source->sprites) {
                const int texture = ResolveTexture(
                    draw.absoluteAssetPath);
                if (texture < 0) {
                    continue;
                }
                UINT textureWidth = 0u;
                UINT textureHeight = 0u;
                DXTEX::DxTextureManager::GetTextureSize(
                    texture,
                    textureWidth,
                    textureHeight);
                if (textureWidth == 0u || textureHeight == 0u) {
                    continue;
                }

                UiVertex* vertices = frame.vertexMapped +
                    drawIndex * kVerticesPerSprite;
                FillQuad(
                    vertices,
                    draw.request,
                    logicalWidth,
                    logicalHeight);
                UiConstants constants{};
                constants.screenSize[0] =
                    static_cast<float>(logicalWidth);
                constants.screenSize[1] =
                    static_cast<float>(logicalHeight);
                constants.texelSize[0] =
                    1.0f / static_cast<float>(textureWidth);
                constants.texelSize[1] =
                    1.0f / static_cast<float>(textureHeight);
                CopyColor(constants.tint, draw.request.tint);
                CopyColor(constants.effectColor, draw.request.effectColor);
                CopyColor(
                    constants.highlightColor,
                    draw.request.highlightColor);
                constants.params0[0] = draw.request.fillAmount;
                constants.params0[1] = draw.request.effectProgress;
                constants.params0[2] = draw.request.effectIntensity;
                constants.params0[3] = draw.request.timeSeconds;
                constants.params1[0] = draw.request.directionX;
                constants.params1[1] = draw.request.directionY;
                constants.params1[2] = static_cast<float>(
                    static_cast<uint32_t>(draw.request.effect));
                constants.params1[3] = static_cast<float>(
                    draw.request.flags);
                std::memcpy(
                    frame.constantMapped + drawIndex * kConstantStride,
                    &constants,
                    sizeof(constants));

                const D3D12_VERTEX_BUFFER_VIEW vertexView{
                    frame.vertices->GetGPUVirtualAddress() +
                        drawIndex * kVerticesPerSprite * sizeof(UiVertex),
                    kVerticesPerSprite * sizeof(UiVertex),
                    sizeof(UiVertex) };
                cmd->IASetVertexBuffers(0u, 1u, &vertexView);
                cmd->SetGraphicsRootConstantBufferView(
                    0u,
                    frame.constants->GetGPUVirtualAddress() +
                        drawIndex * kConstantStride);
                cmd->SetGraphicsRootDescriptorTable(
                    1u,
                    DXTEX::DxTextureManager::GetSrvGpuHandle(texture));
                cmd->DrawInstanced(
                    kVerticesPerSprite,
                    1u,
                    0u,
                    0u);
                ++drawIndex;
            }
            return true;
        }
        catch (...) {
            return false;
        }
    }

    void ShutdownGameUiRenderer() noexcept {
        g.source = nullptr;
        ReleasePipeline();
    }

} // namespace HIKARI::GAME_UI
