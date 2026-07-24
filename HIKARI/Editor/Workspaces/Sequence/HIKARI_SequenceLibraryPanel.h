#pragma once

#include <array>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

class AssetDatabase;

namespace EDITOR {

enum class SequenceLibraryActionKind : uint8_t {
  None,
  NewAsset,
  OpenAsset,
};

struct SequenceLibraryPanelResult {
  SequenceLibraryActionKind action = SequenceLibraryActionKind::None;
  AssetGuid assetGuid{};
};

class SequenceLibraryPanel {
public:
  SequenceLibraryPanelResult Draw(AssetDatabase &assetDatabase,
                                  const AssetGuid &openAssetGuid,
                                  bool actionsAllowed);
  void Select(const AssetGuid &assetGuid) noexcept;

private:
  std::array<char, 128> searchBuffer_{};
  AssetGuid selectedAssetGuid_{};
};

} // namespace EDITOR
} // namespace HIKARI
