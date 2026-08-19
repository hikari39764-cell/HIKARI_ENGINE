param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$contentRoot = Join-Path $RepoRoot 'HIKARI\EngineContent'
$terrainRoot = Join-Path $contentRoot 'Terrain'
$terrainSources = Join-Path $terrainRoot 'Sources'
$terrainTextures = Join-Path $terrainRoot 'Textures'
$sandTextures = Join-Path $contentRoot 'Sand\Textures'

$atlasSize = 2048
$tileSize = 1024
$gutter = 8
$contentSize = $tileSize - $gutter * 2
$rockSaturation = 0.28

function New-ColorMatrix([float]$saturation) {
    $inverse = 1.0 - $saturation
    $r = 0.3086 * $inverse
    $g = 0.6094 * $inverse
    $b = 0.0820 * $inverse
    return [System.Drawing.Imaging.ColorMatrix]::new([single[][]]@(
        [single[]]@(($r + $saturation), $r, $r, 0, 0),
        [single[]]@($g, ($g + $saturation), $g, 0, 0),
        [single[]]@($b, $b, ($b + $saturation), 0, 0),
        [single[]]@(0, 0, 0, 1, 0),
        [single[]]@(0, 0, 0, 0, 1)
    ))
}

function Draw-ScaledImage(
    [System.Drawing.Graphics]$graphics,
    [System.Drawing.Image]$source,
    [System.Drawing.Rectangle]$destination,
    [float]$saturation = 1.0) {

    $sourceRect = [System.Drawing.Rectangle]::new(
        0, 0, $source.Width, $source.Height)
    if ([Math]::Abs($saturation - 1.0) -lt 0.0001) {
        $graphics.DrawImage(
            $source,
            $destination,
            $sourceRect,
            [System.Drawing.GraphicsUnit]::Pixel)
        return
    }

    $attributes = [System.Drawing.Imaging.ImageAttributes]::new()
    try {
        $attributes.SetColorMatrix((New-ColorMatrix $saturation))
        $graphics.DrawImage(
            $source,
            $destination,
            0,
            0,
            $source.Width,
            $source.Height,
            [System.Drawing.GraphicsUnit]::Pixel,
            $attributes)
    } finally {
        $attributes.Dispose()
    }
}

function New-ProcessedBitmap(
    [string]$sourcePath,
    [int]$size,
    [float]$saturation = 1.0) {

    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Terrain atlas input is missing: $sourcePath"
    }

    $source = [System.Drawing.Bitmap]::new($sourcePath)
    try {
        $result = [System.Drawing.Bitmap]::new(
            $size,
            $size,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($result)
        try {
            $graphics.CompositingMode =
                [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.CompositingQuality =
                [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode =
                [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode =
                [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            Draw-ScaledImage `
                $graphics `
                $source `
                ([System.Drawing.Rectangle]::new(0, 0, $size, $size)) `
                $saturation
        } finally {
            $graphics.Dispose()
        }
        return $result
    } finally {
        $source.Dispose()
    }
}

function New-PaddedTile(
    [string]$sourcePath,
    [float]$saturation = 1.0) {

    $scaled = New-ProcessedBitmap $sourcePath $contentSize $saturation
    try {
        $tile = [System.Drawing.Bitmap]::new(
            $tileSize,
            $tileSize,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($tile)
        try {
            $graphics.CompositingMode =
                [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode =
                [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
            $unit = [System.Drawing.GraphicsUnit]::Pixel

            $graphics.DrawImage(
                $scaled,
                [System.Drawing.Rectangle]::new(
                    $gutter, $gutter, $contentSize, $contentSize),
                0, 0, $contentSize, $contentSize, $unit)

            $graphics.DrawImage(
                $scaled,
                [System.Drawing.Rectangle]::new(0, $gutter, $gutter, $contentSize),
                $contentSize - $gutter, 0, $gutter, $contentSize, $unit)
            $graphics.DrawImage(
                $scaled,
                [System.Drawing.Rectangle]::new(
                    $tileSize - $gutter, $gutter, $gutter, $contentSize),
                0, 0, $gutter, $contentSize, $unit)
            $graphics.DrawImage(
                $scaled,
                [System.Drawing.Rectangle]::new($gutter, 0, $contentSize, $gutter),
                0, $contentSize - $gutter, $contentSize, $gutter, $unit)
            $graphics.DrawImage(
                $scaled,
                [System.Drawing.Rectangle]::new(
                    $gutter, $tileSize - $gutter, $contentSize, $gutter),
                0, 0, $contentSize, $gutter, $unit)

            $corners = @(
                @(0, 0, ($contentSize - $gutter), ($contentSize - $gutter)),
                @(($tileSize - $gutter), 0, 0, ($contentSize - $gutter)),
                @(0, ($tileSize - $gutter), ($contentSize - $gutter), 0),
                @(($tileSize - $gutter), ($tileSize - $gutter), 0, 0)
            )
            foreach ($corner in $corners) {
                $graphics.DrawImage(
                    $scaled,
                    [System.Drawing.Rectangle]::new(
                        $corner[0], $corner[1], $gutter, $gutter),
                    $corner[2], $corner[3], $gutter, $gutter, $unit)
            }
        } finally {
            $graphics.Dispose()
        }
        return $tile
    } finally {
        $scaled.Dispose()
    }
}

function Write-Atlas(
    [array]$inputs,
    [string]$outputPath) {

    $atlas = [System.Drawing.Bitmap]::new(
        $atlasSize,
        $atlasSize,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($atlas)
    try {
        $graphics.CompositingMode =
            [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        for ($index = 0; $index -lt $inputs.Count; ++$index) {
            $entry = $inputs[$index]
            $tile = New-PaddedTile $entry.path $entry.saturation
            try {
                $x = ($index % 2) * $tileSize
                $y = [Math]::Floor($index / 2) * $tileSize
                $graphics.DrawImageUnscaled($tile, $x, $y)
            } finally {
                $tile.Dispose()
            }
        }
        $atlas.Save(
            $outputPath,
            [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $atlas.Dispose()
    }
}

New-Item -ItemType Directory -Force -Path $terrainTextures | Out-Null

$colorInputs = @(
    @{ path = Join-Path $sandTextures 'Ground101_1K-JPG_Color.jpg'; saturation = 1.0 },
    @{ path = Join-Path $terrainSources 'Ground048_1K-JPG_Color.jpg'; saturation = 0.82 },
    @{ path = Join-Path $terrainSources 'Grass004_1K-JPG_Color.jpg'; saturation = 0.78 },
    @{ path = Join-Path $terrainSources 'Rock030_1K-JPG_Color.jpg'; saturation = $rockSaturation }
)
$normalInputs = @(
    @{ path = Join-Path $sandTextures 'Ground054_1K-JPG_NormalDX.jpg'; saturation = 1.0 },
    @{ path = Join-Path $terrainSources 'Ground048_1K-JPG_NormalDX.jpg'; saturation = 1.0 },
    @{ path = Join-Path $terrainSources 'Grass004_1K-JPG_NormalDX.jpg'; saturation = 1.0 },
    @{ path = Join-Path $terrainSources 'Rock030_1K-JPG_NormalDX.jpg'; saturation = 1.0 }
)

$colorAtlas = Join-Path $terrainTextures 'TerrainLayer_Atlas_Color.png'
$normalAtlas = Join-Path $terrainTextures 'TerrainLayer_Atlas_NormalDX.png'
$rockColor = Join-Path $terrainTextures 'Rock030_1K-NeutralGray_Color.png'

Write-Atlas $colorInputs $colorAtlas
Write-Atlas $normalInputs $normalAtlas

$rockBitmap = New-ProcessedBitmap `
    (Join-Path $terrainSources 'Rock030_1K-JPG_Color.jpg') `
    1024 `
    $rockSaturation
try {
    $rockBitmap.Save(
        $rockColor,
        [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $rockBitmap.Dispose()
}

foreach ($path in @($colorAtlas, $normalAtlas, $rockColor)) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    Write-Host "$hash  $path"
}
