#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <Windows.h>
#include "Assets/Tasks/HIKARI_AssetImportPublication.h"
#include "Internal/HIKARI_ContractAssert.h"

namespace {
namespace fs = std::filesystem;
void Write(const fs::path& path, const char* value) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << value;
    assert(stream.good());
}
std::string Read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
} // namespace

int main() {
    const auto root = fs::temp_directory_path() / ("HikariPublication-" + std::to_string(GetCurrentProcessId()) + "-" +
                                                   std::to_string(GetTickCount64()));
    assert(fs::create_directory(root));
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    } cleanup{root};
    const auto artifact = root / L"Library/Imported/材质/テクスチャ.htex";
    const auto manifest = root / "ProjectSettings/item.json";
    Write(artifact, "old texture");
    Write(manifest, "old manifest");
    fs::path abandoned;
    {
        HIKARI::AssetImportPublication publication(root);
        abandoned = publication.StageFile(artifact);
        Write(abandoned, "canceled");
        assert(Read(artifact) == "old texture");
        assert(publication.ReadPreparedFile(artifact) == abandoned);
    }
    assert(!fs::exists(abandoned) && Read(artifact) == "old texture");
    {
        HIKARI::AssetImportPublication publication(root);
        Write(publication.StageFile(artifact), "new texture");
        Write(publication.StageFile(manifest), "new manifest");
        assert(Read(artifact) == "old texture" && Read(manifest) == "old manifest");
        std::string error;
        assert(publication.Publish(error));
        assert(Read(artifact) == "new texture" && Read(manifest) == "new manifest");
        assert(!publication.Publish(error));
    }
    {
        HIKARI::AssetImportPublication publication(root);
        Write(publication.StageFile(artifact), "must not publish");
        publication.StageFile(manifest); // 必須出力が無い場合、古い組を保持する。
        std::string error;
        assert(!publication.Publish(error));
        assert(Read(artifact) == "new texture" && Read(manifest) == "new manifest");
    }
    {
        HIKARI::AssetImportPublication publication(root);
        const auto optional = root / "Library/Imported/item/optional.hcmesh";
        publication.StageFile(optional);
        publication.DiscardFile(optional);
        publication.DeleteOnPublish(artifact);
        Write(publication.StageFile(manifest), "without texture");
        std::string error;
        assert(publication.Publish(error));
        assert(!fs::exists(artifact) && !fs::exists(optional));
        assert(Read(manifest) == "without texture");
    }
    {
        HIKARI::AssetImportPublication publication(root);
        Write(publication.StageFile(manifest), "validated revision");
        bool current = false;
        publication.AddValidation([&](std::string& error) {
            if (!current)
                error = "source changed";
            return current;
        });
        std::string error;
        assert(!publication.Publish(error));
        assert(Read(manifest) == "without texture");
        current = true;
        assert(publication.Publish(error));
        assert(Read(manifest) == "validated revision");
    }
    {
        HIKARI::AssetImportPublication publication(root);
        bool rejected = false;
        try {
            publication.StageFile(root.parent_path() / "outside.htex");
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        assert(rejected);
    }
    {
        const auto project = root / "project";
        const auto library = root / "separate-library";
        const auto metadata = root / "separate-meta";
        fs::create_directories(project);
        HIKARI::AssetImportPublication publication(project, library, metadata);
        const auto output = library / "Imported/item/texture.htex";
        const auto meta = metadata / "item.json";
        const auto stagedOutput = publication.StageFile(output);
        const auto stagedMeta = publication.StageFile(meta);
        assert(*stagedOutput.lexically_relative(library).begin() == ".ImportStaging");
        assert(*stagedMeta.lexically_relative(metadata).begin() == ".ImportStaging");
        Write(stagedOutput, "external cache");
        Write(stagedMeta, "external metadata");
        std::string error;
        assert(publication.Publish(error));
        assert(Read(output) == "external cache" && Read(meta) == "external metadata");
    }
    return 0;
}
