#pragma once

#include <filesystem>
#include <string>

#include "Runtime/Binding/HIKARI_RuntimeBindingCatalog.h"

namespace HIKARI::BINDING {

std::filesystem::path GetCatalogPath(const std::filesystem::path& projectRoot);

bool LoadCatalog(const std::filesystem::path& path, Catalog& outCatalog, std::string& outError);

bool SaveCatalog(const std::filesystem::path& path, const Catalog& catalog, std::string& outError);

} // namespace HIKARI::BINDING
