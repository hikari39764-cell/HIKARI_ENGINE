# Painterly stroke engine content

This package contains one original dry-brush field and its matching shallow
normal map for the painterly material template. The image was generated for
HIKARI with the built-in `image_gen` tool. It is not the texture from Aqu's
article, a copy of an article screenshot, or a third-party CC0 asset.
`Sources/Provenance.json` retains the generation prompt and processing intent.

The two runtime textures are 1024 x 1024, opaque, and **linear data**:

- `StrokeMask`: scalar coverage in R, BC7 with a complete mip chain. This is
  intentionally a grayscale field, not a pre-thresholded cutout. At small
  footprints its contrast converges toward average coverage; the shell LOD
  and material coverage threshold own the final silhouette.
- `StrokeNormalDX`: DirectX tangent-space normal, BC5 with a complete mip
  chain. The shared texture importer decodes and normalizes directions before
  filtering, then normalizes each mip. The runtime reconstructs positive Z
  from XY. Do not interpret the BC5 blue channel as an authored normal.

`Sources/StrokeHeight.png` is the 16-bit height used to bake the normal.
Coverage is not assumed to be physical height: the bake removes broad
pigment-value bias with a wrapped 32-pixel Gaussian and uses softened local
ridges (1.5-pixel Gaussian) for restrained **synthetic** relief. This is an
artistic height interpretation, not recovered physical paint geometry. Height
has no runtime reader and is therefore not registered or cooked separately.

The retained generated source is 1254 x 1254. `Sources/BakeStrokeTextures.py`
resamples it, removes smooth periodic-boundary discrepancy, and derives both
outputs. It needs NumPy and Pillow from the existing authoring environment,
with no additional packages. Wrapped central differences use +X to the right
and image +Y down; the DirectX normal is proportional to `(-dH/dX,-dH/dY,1)`.
The rejected AI-generated normal candidate is not shipped.

From the repository root:

```powershell
python HIKARI/EngineContent/Painterly/Sources/BakeStrokeTextures.py
out/build/editor-release/tools/HIKARI_EngineContentTextureCookTool.exe HIKARI/EngineContent --manifest Painterly/BuiltinAssets.json
```

Update the manifest hashes after an intentional source change and cook.
`Sources/BakeReport.json` records source-image checks: dimensions, wrap-edge
differences compared with interior differences, neutral normal behavior,
channel bias and quantized direction error. These checks establish technical
data consistency; they do not establish scene appearance or GPU performance.
