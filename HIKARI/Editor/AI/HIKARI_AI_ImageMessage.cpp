#include "HIKARI_AI_ToolProtocol.h"
#include "HIKARI_AI_ChatClient.h"
#include "HIKARI_AI_RequestContext.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <json.hpp>
#ifdef _WIN32
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib, "windowscodecs.lib")
#endif

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {
// 転送の安全上限。
constexpr size_t kMaxImageBytes = 16u * 1024u * 1024u;

bool RequiresReasoningReplay(const AiChatConfig& config) {
    if (!config.editorTools)
        return false;
    std::string endpoint = config.baseUrl;
    std::transform(endpoint.begin(), endpoint.end(), endpoint.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    const auto scheme = endpoint.find("://");
    if (scheme != std::string::npos)
        endpoint.erase(0, scheme + 3);
    const auto suffix = endpoint.find_first_of("/?#");
    if (suffix != std::string::npos)
        endpoint.resize(suffix);
    const auto port = endpoint.find(':');
    if (port != std::string::npos)
        endpoint.resize(port);
    std::string model = config.model;
    std::transform(model.begin(), model.end(), model.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return endpoint == "api.deepseek.com" && (model == "deepseek-flash" || model.starts_with("deepseek-v4-"));
}

size_t SentTextBytes(const nlohmann::json& request) {
    size_t bytes = request.contains("tools") ? request["tools"].dump().size() : 0;
    if (!request.contains("messages") || !request["messages"].is_array())
        return bytes;
    for (const auto& message : request["messages"]) {
        const auto content = message.find("content");
        if (content != message.end()) {
            if (content->is_string())
                bytes += content->get_ref<const std::string&>().size();
            else if (content->is_array()) {
                for (const auto& part : *content)
                    if (part.is_object() && part.value("type", std::string{}) == "text" && part.contains("text") &&
                        part["text"].is_string())
                        bytes += part["text"].get_ref<const std::string&>().size();
            }
        }
        for (const auto* key : {"tool_calls", "reasoning_content"})
            if (message.contains(key))
                bytes += message[key].is_string() ? message[key].get_ref<const std::string&>().size()
                                                  : message[key].dump().size();
    }
    return bytes;
}

bool DecodeImage(const std::string& bytes, std::string& error) {
#ifdef _WIN32
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
        error = "无法初始化图片解码器。";
        return false;
    }
    const bool valid = [&] {
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())),
                                                static_cast<DWORD>(bytes.size()))) ||
            FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame)))
            return false;
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || !width || !height ||
            static_cast<uint64_t>(width) * height > 16 * 1024 * 1024)
            return false;
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)))
            return false;
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
        return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    }();
    if (SUCCEEDED(apartment))
        CoUninitialize();
    if (!valid)
        error = "图片无法完整解码，或超过 1600 万像素。请使用有效的 PNG/JPEG 图片。";
    return valid;
#else
    (void)bytes;
    (void)error;
    return true;
#endif
}

std::string Base64(const std::string& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
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
} // namespace

bool ReadAiImageDataUri(const std::string& path, std::string& uri, std::string& error) {
    uri.clear();
    error.clear();
    const std::filesystem::path imagePath(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::ifstream file(imagePath, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "无法读取图片：" + path;
        return false;
    }
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
    else {
        error = "图片内容不是有效的 PNG/JPEG/GIF/WebP：" + path;
        return false;
    }
    if (!DecodeImage(bytes, error))
        return false;
    uri = "data:" + mime + ";base64," + Base64(bytes);
    return true;
}

bool BuildAiChatRequest(const AiChatConfig& config, const std::vector<AiChatMessage>& messages, bool stream,
                        std::string& body, std::string& error, AiChatRequestStats* stats) {
    error.clear();
    body.clear();
    AiChatRequestStats measured;
    for (const auto& message : messages) {
        measured.sourceTextBytes += message.content.size() + (message.role == "tool" ? message.data.dump().size() : 0);
        for (const char* field : {"_toolCalls", "_reasoningContent"})
            if (message.data.contains(field))
                measured.sourceTextBytes += message.data[field].dump().size();
    }
    if (config.reasoningContentPolicy == AiReasoningContentPolicy::Omit && RequiresReasoningReplay(config)) {
        error = "当前 DeepSeek 官方接口的原生编辑器工具协议要求保留完整 reasoning_content；请将推理策略设为保留。";
        return false;
    }
    const auto context = BuildAiRequestContext(messages, config.toolHistoryKiB, config.requestContext,
                                               config.requestContextPrefixMessages, config.reasoningContentPolicy);
    nlohmann::json request{{"model", config.model}, {"stream", stream}, {"messages", nlohmann::json::array()}};
    if (stream && config.requestUsage)
        request["stream_options"] = {{"include_usage", true}};
    if (config.editorTools) {
        request["tools"] = BuildAiTools();
        request["tool_choice"] = "auto";
        measured.sourceTextBytes += request["tools"].dump().size();
    }
    std::set<std::string> sentImages;
    std::vector<nlohmann::json> pendingImages;
    auto flushImages = [&] {
        for (auto& item : pendingImages)
            request["messages"].push_back(std::move(item));
        pendingImages.clear();
    };
    for (const auto& message : context) {
        // Native calls and their result IDs stay paired within the current task.
        const bool nativeResult = message.data.contains("_toolCallId");
        if (!nativeResult)
            flushImages();
        if (message.data.contains("_toolCalls")) {
            nlohmann::json outgoing{
                {"role", "assistant"}, {"content", message.content}, {"tool_calls", message.data["_toolCalls"]}};
            if (message.data.contains("_reasoningContent"))
                outgoing["reasoning_content"] = message.data["_reasoningContent"];
            request["messages"].push_back(std::move(outgoing));
            continue;
        }
        if (nativeResult)
            request["messages"].push_back(
                {{"role", "tool"}, {"tool_call_id", message.data["_toolCallId"]}, {"content", message.content}});
        if (nativeResult && message.images.empty())
            continue;
        if (message.images.empty()) {
            std::string content = message.content;
            request["messages"].push_back(
                {{"role", message.role == "tool" ? "user" : message.role}, {"content", std::move(content)}});
            if (message.data.contains("_reasoningContent"))
                request["messages"].back()["reasoning_content"] = message.data["_reasoningContent"];
            continue;
        }
        if (!config.imageInputEnabled) {
            error = "当前模型配置关闭了图片输入。请启用图片输入或移除附件。";
            return false;
        }
        // Chat Completions の画像は user メッセージに限定する。
        nlohmann::json parts = nlohmann::json::array();
        std::string imageText = message.content;
        if (nativeResult) {
            imageText = "Preview images for editor tool call " + message.data.value("_toolCallId", std::string{}) + ":";
            for (size_t imageIndex = 0; imageIndex < message.images.size(); ++imageIndex) {
                const auto& image = message.images[imageIndex];
                imageText += " Image " + std::to_string(imageIndex + 1);
                if (!image.label.empty())
                    imageText += ": " + image.label;
                imageText += ";";
            }
        }
        parts.push_back({{"type", "text"}, {"text", std::move(imageText)}});
        for (const auto& image : message.images) {
            if (!sentImages.insert(image.path).second)
                continue;
            std::string uri;
            if (!ReadAiImageDataUri(image.path, uri, error))
                return false;
            ++measured.sentImages;
            parts.push_back(
                {{"type", "image_url"}, {"image_url", {{"url", std::move(uri)}, {"detail", image.detail}}}});
        }
        // 全 tool_call_id の返答を連続させてから画像を渡す。
        nlohmann::json imageMessage{{"role", "user"}, {"content", std::move(parts)}};
        if (nativeResult)
            pendingImages.push_back(std::move(imageMessage));
        else
            request["messages"].push_back(std::move(imageMessage));
    }
    flushImages();
    measured.sentTextBytes = SentTextBytes(request);
    body = request.dump();
    if (stats)
        *stats = measured;
    return true;
}
} // namespace HIKARI::EDITOR::AI_ASSISTANT
