tinyexr (v1 single-header API) from the tinyexr 3.2.0 release, with its bundled miniz

tinyexr
  Source: https://github.com/syoyo/tinyexr
  Release: https://github.com/syoyo/tinyexr/releases/tag/v3.2.0
  License: BSD 3-Clause (LICENSE; OpenEXR-derived code keeps its notice inside tinyexr.h)
  Copied: tinyexr.h (the v1 header) and the two headers its implementation includes, exr_reader.hh and
          streamreader.hh (BSD-3-Clause); the v3 C11 sources are not vendored

miniz (bundled by tinyexr 3.2.0 under deps/miniz)
  License: MIT (miniz/LICENSE)
  Copied: miniz.c, miniz.h

HIKARI_tinyexr_impl.cc is HIKARI's single implementation unit (TINYEXR_IMPLEMENTATION).
Used only by Modules/TerrainKit (EXR height and mask import/export, deflate and CRC32 of terrain cell files).
