"""Reproduce the linear painterly maps from the retained AI-generated marks.

Requires the existing authoring Python environment (NumPy and Pillow). No
image generator is run here; all operations are deterministic data processing.
"""

from pathlib import Path
import hashlib
import json

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
SIZE = 1024
NORMAL_RELIEF_PIXELS = 2.0


def periodic_component(values):
    """Remove the smooth boundary discrepancy without mirroring the marks."""
    boundary = np.zeros_like(values)
    boundary[0] = values[-1] - values[0]
    boundary[-1] = -boundary[0]
    boundary[:, 0] += values[:, -1] - values[:, 0]
    boundary[:, -1] -= values[:, -1] - values[:, 0]
    rows, columns = values.shape
    denominator = (
        2 * np.cos(2 * np.pi * np.arange(rows)[:, None] / rows)
        + 2 * np.cos(2 * np.pi * np.arange(columns)[None, :] / columns)
        - 4
    )
    denominator[0, 0] = 1
    spectrum = np.fft.fft2(boundary) / denominator
    spectrum[0, 0] = 0
    return values - np.fft.ifft2(spectrum).real


def gaussian_wrap(values, sigma):
    fy = np.fft.fftfreq(values.shape[0])[:, None]
    fx = np.fft.fftfreq(values.shape[1])[None, :]
    kernel = np.exp(-2 * np.pi**2 * sigma**2 * (fx * fx + fy * fy))
    return np.fft.ifft2(np.fft.fft2(values) * kernel).real


def normal_dx(height):
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    # Image coordinates: +X right, +Y down. DX uses -dH/dY in green;
    # the corresponding OpenGL texture has the opposite green channel.
    normal = np.stack((-dx * NORMAL_RELIEF_PIXELS, -dy * NORMAL_RELIEF_PIXELS, np.ones_like(height)), axis=2)
    return normal / np.linalg.norm(normal, axis=2, keepdims=True)


def hash_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def seam_statistics(values):
    horizontal = np.abs(np.diff(values, axis=1))
    vertical = np.abs(np.diff(values, axis=0))
    return {
        "wrap_x_mean_difference": float(np.abs(values[:, -1] - values[:, 0]).mean()),
        "interior_x_mean_difference": float(horizontal.mean()),
        "wrap_y_mean_difference": float(np.abs(values[-1] - values[0]).mean()),
        "interior_y_mean_difference": float(vertical.mean()),
    }


def main():
    raw_path = ROOT / "Sources/StrokeMarks_Generated.png"
    generated = Image.open(raw_path).convert("L").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    raw = np.asarray(generated, dtype=np.float64) / 255
    periodic = periodic_component(raw)
    lower, upper = np.percentile(periodic, [0.5, 99.5])
    mask = np.clip((periodic - lower) / (upper - lower), 0, 1) * 0.84 + 0.08
    mask_u8 = np.rint(mask * 255).astype(np.uint8)

    # Coverage is NOT measured height. Define separate, shallow stylized relief:
    # remove broad pigment-value variation, keep softened local brush ridges,
    # then compress their amplitude symmetrically about the neutral plane.
    local_ridges = gaussian_wrap(periodic, 1.5) - gaussian_wrap(periodic, 32.0)
    ridge_scale = float(np.percentile(np.abs(local_ridges), 98))
    height = 0.5 + 0.3 * np.tanh(local_ridges / ridge_scale)
    height_u16 = np.rint(height * 65535).astype(np.uint16)
    # Bake from the exact saved 16-bit height so the pair remains reproducible.
    height = height_u16.astype(np.float64) / 65535
    normal = normal_dx(height)
    normal_u8 = np.rint((normal * 0.5 + 0.5) * 255).astype(np.uint8)

    outputs = {
        "mask": ROOT / "Textures/StrokeMask.png",
        "height": ROOT / "Sources/StrokeHeight.png",
        "normal": ROOT / "Textures/StrokeNormalDX.png",
    }
    for path in outputs.values():
        path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(mask_u8).save(outputs["mask"])
    Image.fromarray(height_u16).save(outputs["height"])
    Image.fromarray(normal_u8).save(outputs["normal"])

    decoded = normal_u8.astype(np.float64) / 255 * 2 - 1
    error = np.abs(np.linalg.norm(decoded, axis=2) - 1)
    flat = normal_dx(np.full((8, 8), 0.5))
    assert np.allclose(flat, [0, 0, 1])
    assert float(error.max()) < 0.008
    assert float(normal[:, :, 2].min()) > 0.9
    assert np.max(np.abs(normal[:, :, :2].mean(axis=(0, 1)))) < 0.001
    assert np.corrcoef(mask.ravel(), height.ravel())[0, 1] < 0.98

    report = {
        "dimensions": [SIZE, SIZE],
        "source_sha256": hash_file(raw_path),
        "outputs": {name: {"path": path.relative_to(ROOT).as_posix(), "sha256": hash_file(path)} for name, path in outputs.items()},
        "mask": {"seams": seam_statistics(mask_u8.astype(float) / 255), "range": [int(mask_u8.min()), int(mask_u8.max())]},
        "height": {"seams": seam_statistics(height), "coverage_correlation": float(np.corrcoef(mask.ravel(), height.ravel())[0, 1])},
        "normal": {
            "mean_signed_xy": normal[:, :, :2].mean(axis=(0, 1)).tolist(),
            "minimum_z": float(normal[:, :, 2].min()),
            "maximum_length_error_after_rgb8_quantization": float(error.max()),
            "flat_encoded_rgb": [128, 128, 255],
            "seams": seam_statistics(normal_u8.astype(float) / 255),
        },
        "normal_relief_pixels": NORMAL_RELIEF_PIXELS,
    }
    report_path = ROOT / "Sources/BakeReport.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
