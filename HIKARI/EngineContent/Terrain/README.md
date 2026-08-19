# Stylized Terrain Engine Content

This package provides the built-in stylized terrain-layer atlas, including a
neutral-grey Rock030 layer intended for terrain sculpting. Standalone
procedural boulders are not part of this package. All upstream images are
ambientCG CC0-1.0 assets. The runtime consumes only the hash-locked HTEX v2
artifacts.

The 2x2 atlas layout is stable:

1. top-left: Ground101 sand colour / Ground054 NormalDX;
2. top-right: Ground048 dirt;
3. bottom-left: Grass004 short grass;
4. bottom-right: Rock030 neutral-grey stone.

Each 1024 tile contains an 8-pixel wrapped gutter. Shaders must sample the
1008-pixel content rectangle and may not treat the atlas as one wrapping
texture. `Tools/Scripts/Build-TerrainLayerAtlases.ps1` reproduces the source
PNG files. Height/displacement maps are intentionally excluded: terrain
geometry remains owned by `TerrainSurfaceComponent`.
