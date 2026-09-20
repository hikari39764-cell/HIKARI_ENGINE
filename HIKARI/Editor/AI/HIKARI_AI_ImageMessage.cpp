#include "HIKARI_AI_ChatClient.h"
#include "HIKARI_AI_RequestContext.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <json.hpp>
#ifdef _WIN32
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib,"windowscodecs.lib")
#endif

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {
// 転送の安全上限。画像履歴全体ではなく直近の画像だけをモデルに渡す。
constexpr size_t kMaxImageBytes = 16u * 1024u * 1024u;
constexpr size_t kMaxPreviewImages = 6u;

bool DecodeImage(const std::string& bytes,std::string& error) {
#ifdef _WIN32
    const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if (FAILED(apartment) && apartment!=RPC_E_CHANGED_MODE) { error="无法初始化图片解码器。";return false; }
    const bool valid=[&] {
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())),static_cast<DWORD>(bytes.size()))) ||
            FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)) ||
            FAILED(decoder->GetFrame(0,&frame))) return false;
        UINT width=0,height=0;
        if (FAILED(frame->GetSize(&width,&height)) || !width || !height || static_cast<uint64_t>(width)*height>16*1024*1024) return false;
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return false;
        std::vector<BYTE> pixels(static_cast<size_t>(width)*height*4);
        return SUCCEEDED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data()));
    }();
    if (SUCCEEDED(apartment)) CoUninitialize();
    if (!valid) error="图片无法完整解码，或超过 1600 万像素。请使用有效的 PNG/JPEG 图片。";
    return valid;
#else
    (void)bytes;(void)error;return true;
#endif
}

std::string Base64(const std::string& bytes) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned a = static_cast<unsigned char>(bytes[i]);
        const unsigned b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const unsigned c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        const unsigned value = (a << 16) | (b << 8) | c;
        output += alphabet[(value >> 18) & 63];
        output += alphabet[(value >> 12) & 63];
        output += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
        output += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
    }
    return output;
}
}

bool ReadAiImageDataUri(const std::string& path, std::string& uri,
    std::string& error) {
    uri.clear();
    error.clear();
    const std::filesystem::path imagePath(std::u8string(
        reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::ifstream file(imagePath, std::ios::binary | std::ios::ate);
    if (!file) { error = "无法读取图片：" + path; return false; }
    const auto length = file.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > kMaxImageBytes) {
        error = "图片为空或超过 16 MiB：" + path;
        return false;
    }
    std::string bytes(static_cast<size_t>(length), '\0');
    file.seekg(0);
    if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        error = "图片读取不完整：" + path;
        return false;
    }
    std::string mime;
    if (bytes.size() >= 8 && bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0)
        mime = "image/png";
    else if (bytes.size() >= 3 && bytes.compare(0, 3, "\xff\xd8\xff", 3) == 0)
        mime = "image/jpeg";
    else if (bytes.size() >= 6 && (bytes.compare(0, 6, "GIF87a") == 0 || bytes.compare(0, 6, "GIF89a") == 0))
        mime = "image/gif";
    else if (bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 && bytes.compare(8, 4, "WEBP") == 0)
        mime = "image/webp";
    else { error = "图片内容不是有效的 PNG/JPEG/GIF/WebP：" + path; return false; }
    if (!DecodeImage(bytes,error)) return false;
    uri = "data:" + mime + ";base64," + Base64(bytes);
    return true;
}

bool BuildAiChatRequest(const AiChatConfig& config,
    const std::vector<AiChatMessage>& messages, bool stream,
    std::string& body, std::string& error, AiChatRequestStats* stats) {
    error.clear();
    body.clear();
    AiChatRequestStats measured;
    for (const auto& message : messages)
        measured.sourceTextBytes += message.content.size() + (message.role == "tool" ? message.data.dump().size() : 0);
    const auto context = BuildAiRequestContext(messages, config.toolHistoryKiB);
    size_t imageCount = 0;
    for (const auto& message : context) if (message.role == "tool") imageCount += message.images.size();
    size_t skip = imageCount > kMaxPreviewImages ? imageCount - kMaxPreviewImages : 0;
    nlohmann::json request{{"model", config.model}, {"stream", stream},
        {"messages", nlohmann::json::array()}};
    if (stream && config.requestUsage) request["stream_options"] = {{"include_usage", true}};
    std::set<std::string> sentImages;
    for (const auto& message : context) {
        measured.sentTextBytes += message.content.size();
        if (message.images.empty()) {
            std::string content=message.content;
            request["messages"].push_back({{"role", message.role == "tool" ? "user" : message.role}, {"content", std::move(content)}});
            continue;
        }
        if (!config.imageInputEnabled) {
            error = "当前模型配置关闭了图片输入。请启用图片输入或移除附件。";
            return false;
        }
        // Chat Completions の画像は user メッセージに限定する。
        nlohmann::json parts = nlohmann::json::array();
        parts.push_back({{"type", "text"}, {"text", message.content}});
        size_t omitted = 0;
        for (const auto& image : message.images) {
            if (skip && message.role == "tool") { --skip; ++omitted; continue; }
            if (!sentImages.insert(image.path).second) continue;
            std::string uri;
            if (!ReadAiImageDataUri(image.path, uri, error)) return false;
            ++measured.sentImages;
            parts.push_back({{"type", "image_url"}, {"image_url",
                {{"url", std::move(uri)}, {"detail", image.detail}}}});
        }
        if (omitted) parts.push_back({{"type", "text"}, {"text",
            std::to_string(omitted) + " images omitted by the per-request preview limit; use context.image to review their paths."}});
        request["messages"].push_back({{"role", "user"}, {"content", std::move(parts)}});
    }
    body = request.dump();
    if (stats) *stats = measured;
    return true;
}
}
