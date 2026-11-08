FastNoise2 1.1.1 + FastSIMD (pinned) for the terrain data core

FastNoise2
  Source: https://github.com/Auburn/FastNoise2
  Release: https://github.com/Auburn/FastNoise2/releases/tag/v1.1.1
  License: MIT (LICENSE)
  Copied: include/, src/ (src/CMakeLists.txt removed; tools, tests, util, CPM not vendored)

FastSIMD (dependency of FastNoise2, the commit pinned by FastNoise2 1.1.1's src/CMakeLists.txt)
  Source: https://github.com/Auburn/FastSIMD
  Commit: 16450dae9528727e500e7254f635a671f9c7ee2d
  License: MIT (FastSIMD/LICENSE)
  Copied: include/, src/, dispatch/impl, dispatch/cmake/*.in, cmake/ (dispatch/CMakeLists.txt removed)

HIKARI builds both from ThirdParty/CMakeLists.txt without network access and without try_compile:
x64 MSVC, dispatch feature sets SSE2 / SSE41 / AVX2 / AVX512 (relaxed float), /W0.
Used only by Modules/TerrainKit (procedural height layers). No engine runtime target links it.
