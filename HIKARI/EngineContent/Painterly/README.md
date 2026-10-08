# Painterly stroke engine content

This package holds the two textures used by the fixed-function painterly shell
material (`MaterialTemplateId::PainterlyShell`, `Shell` node). Both are
generated procedurally by `Sources/GenerateStrokeTextures.py`; no image
generator and no third-party texture is involved. `Sources/Provenance.json`
records the method and `Sources/GenerateReport.json` the measured statistics.

The two runtime textures are 1024 x 1024, tileable, opaque, and **linear data**:

- `StrokeMask`: dense dry-brush scumble in R (mean about 0.38, range 20..223),
  BC7 with a complete mip chain. It is deliberately not a set of isolated
  strokes: the shell shows it only along the silhouette, dithered per layer,
  so the stacked shells read as a soft brushed halo. Its value also drives the
  stroke colour variation on the base surface.
- `StrokeNormalDX`: DirectX tangent-space normal of the softened mask relief,
  BC5 with a complete mip chain. The runtime reconstructs Z from XY and
  rotates the direction by the per-layer stroke UV rotation.

Regenerate and cook from the repository root:

```powershell
python HIKARI/EngineContent/Painterly/Sources/GenerateStrokeTextures.py
out/build/editor-release/tools/HIKARI_EngineContentTextureCookTool.exe HIKARI/EngineContent --manifest Painterly/BuiltinAssets.json
```

The cook tool prints the source and cooked SHA-256 to write back into
`BuiltinAssets.json` after an intentional change. The report checks data
consistency only (seams, range, normal statistics); appearance is validated by
rendering the `PainterlyShell` reference fixture.
