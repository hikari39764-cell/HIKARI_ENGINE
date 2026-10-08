"""Generate the painterly stroke mask and its normal map procedurally (pure Python, no NumPy / Pillow).

The mask is a dense dry-brush scumble: thousands of short, soft, low-alpha dabs along a dominant
direction with some crossing ones, their density gated by broad patch noise, pigment built up with a
saturating curve and a light grain. This is the character the shell technique needs: no isolated
strokes, mid-grey average, soft light/dark patches that become a fuzzy halo when eight shells stack.

Outputs (1024 x 1024, tileable, linear data):
  Textures/StrokeMask.png      8-bit grey coverage (R)
  Textures/StrokeNormalDX.png  8-bit RGB DirectX tangent-space normal from the mask relief
  Sources/GenerateReport.json  seams, range and normal statistics for the manifest

Run from the repository root:
  python HIKARI/EngineContent/Painterly/Sources/GenerateStrokeTextures.py
"""
import hashlib
import json
import math
import random
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIZE = 1024
SEED = 3
DABS = 16000
DOMINANT_ANGLE = math.radians(80.0)
RELIEF = 4.0


def write_png(path, size, rows, channels):
    color = {1: 0, 3: 2}[channels]
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, color, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def noise2d(nx, ny, seed):
    """Periodic value noise on an nx * ny lattice with smoothstep interpolation; u, v in tiles."""
    generator = random.Random(seed)
    grid = [generator.random() for _ in range(nx * ny)]

    def sample(u, v):
        x = (u % 1.0) * nx
        y = (v % 1.0) * ny
        i, j = int(x), int(y)
        tx, ty = x - i, y - j
        tx = tx * tx * (3 - 2 * tx)
        ty = ty * ty * (3 - 2 * ty)
        i1, j1 = (i + 1) % nx, (j + 1) % ny
        a = grid[j * nx + i] * (1 - tx) + grid[j * nx + i1] * tx
        b = grid[j1 * nx + i] * (1 - tx) + grid[j1 * nx + i1] * tx
        return a * (1 - ty) + b * ty

    return sample


def blur3(src):
    out = [0.0] * (SIZE * SIZE)
    for y in range(SIZE):
        ym, yp = ((y - 1) % SIZE) * SIZE, ((y + 1) % SIZE) * SIZE
        y0 = y * SIZE
        for x in range(SIZE):
            xm, xp = (x - 1) % SIZE, (x + 1) % SIZE
            out[y0 + x] = (src[y0 + x] * 4 + src[y0 + xm] * 2 + src[y0 + xp] * 2 + src[ym + x] * 2 + src[yp + x] * 2
                           + src[ym + xm] + src[ym + xp] + src[yp + xm] + src[yp + xp]) / 16.0
    return out


def seam_statistics(values):
    def mean(items):
        items = list(items)
        return sum(items) / max(len(items), 1)

    wrap_x = mean(abs(values[y * SIZE + SIZE - 1] - values[y * SIZE]) for y in range(SIZE))
    wrap_y = mean(abs(values[(SIZE - 1) * SIZE + x] - values[x]) for x in range(SIZE))
    interior_x = mean(abs(values[y * SIZE + x + 1] - values[y * SIZE + x]) for y in range(0, SIZE, 8)
                      for x in range(SIZE - 1))
    interior_y = mean(abs(values[(y + 1) * SIZE + x] - values[y * SIZE + x]) for y in range(SIZE - 1)
                      for x in range(0, SIZE, 8))
    return {"wrap_x_mean_difference": wrap_x, "interior_x_mean_difference": interior_x,
            "wrap_y_mean_difference": wrap_y, "interior_y_mean_difference": interior_y}


def main():
    generator = random.Random(SEED)
    patch = noise2d(5, 5, generator.randrange(1 << 30))
    patch2 = noise2d(12, 12, generator.randrange(1 << 30))
    patch3 = noise2d(27, 27, generator.randrange(1 << 30))
    grain = noise2d(512, 512, generator.randrange(1 << 30))

    pigment = [0.0] * (SIZE * SIZE)
    placed = 0
    while placed < DABS:
        x = generator.uniform(0, SIZE)
        y = generator.uniform(0, SIZE)
        density = 0.45 * patch(x / SIZE, y / SIZE) + 0.35 * patch2(x / SIZE, y / SIZE) + 0.2 * patch3(x / SIZE, y / SIZE)
        if generator.random() > (0.08 + 0.92 * density) ** 1.6:
            continue
        placed += 1
        if generator.random() < 0.22:
            angle = DOMINANT_ANGLE + math.radians(generator.choice((-62, 62))) + generator.gauss(0, 0.25)
        else:
            angle = DOMINANT_ANGLE + generator.gauss(0, 0.2)
        length = generator.uniform(10, 60)
        width = generator.uniform(2.0, 7.0)
        alpha = generator.uniform(0.03, 0.10) * (0.5 + density) * (3.0 / width)
        ca, sa = math.cos(angle), math.sin(angle)
        steps = int(length / 0.7) + 1
        across = int(math.ceil(width / 0.7))
        for i in range(steps):
            t = i / max(steps - 1, 1)
            fade = 1.0 - (2 * t - 1) ** 4
            bx = x + (t - 0.5) * length * ca
            by = y + (t - 0.5) * length * sa
            for j in range(-across, across + 1):
                s = j * 0.7
                if abs(s) > width:
                    continue
                weight = 1.0 - (s / width) ** 2
                px = int(bx - s * sa) % SIZE
                py = int(by + s * ca) % SIZE
                pigment[py * SIZE + px] += alpha * fade * weight

    pigment = blur3(blur3(pigment))
    mask = [0.0] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            index = y * SIZE + x
            value = 1.0 - math.exp(-pigment[index] * 1.9)
            value = 0.12 + 0.78 * value + 0.08 * (grain(x / SIZE, y / SIZE) - 0.5)
            mask[index] = max(min(value, 1.0), 0.0)
    mask_u8 = [int(round(v * 255)) for v in mask]
    mask = [v / 255.0 for v in mask_u8]

    # The relief is the softened mask itself: lighter pigment stands a little higher. Wrapped central
    # differences, DirectX green (+Y down in the image), Z reconstructed on the GPU from XY.
    height = blur3(mask)
    normal_rows = []
    min_z = 1.0
    mean_x = mean_y = 0.0
    for y in range(SIZE):
        row = bytearray()
        ym, yp = ((y - 1) % SIZE) * SIZE, ((y + 1) % SIZE) * SIZE
        y0 = y * SIZE
        for x in range(SIZE):
            xm, xp = (x - 1) % SIZE, (x + 1) % SIZE
            gx = (height[y0 + xp] - height[y0 + xm]) * 0.5 * RELIEF
            gy = (height[yp + x] - height[ym + x]) * 0.5 * RELIEF
            inv = 1.0 / math.sqrt(gx * gx + gy * gy + 1.0)
            nx, ny, nz = -gx * inv, -gy * inv, inv
            min_z = min(min_z, nz)
            mean_x += nx
            mean_y += ny
            row += bytes((int(round((nx * 0.5 + 0.5) * 255)), int(round((ny * 0.5 + 0.5) * 255)),
                          int(round((nz * 0.5 + 0.5) * 255))))
        normal_rows.append(bytes(row))

    outputs = {"mask": ROOT / "Textures/StrokeMask.png", "normal": ROOT / "Textures/StrokeNormalDX.png"}
    write_png(outputs["mask"], SIZE, [bytes(mask_u8[y * SIZE:(y + 1) * SIZE]) for y in range(SIZE)], 1)
    write_png(outputs["normal"], SIZE, normal_rows, 3)
    report = {
        "generator": "GenerateStrokeTextures.py",
        "seed": SEED,
        "dimensions": [SIZE, SIZE],
        "outputs": {name: {"path": path.relative_to(ROOT).as_posix(),
                           "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for name, path in outputs.items()},
        "mask": {"seams": seam_statistics(mask), "range": [min(mask_u8), max(mask_u8)],
                 "mean": sum(mask) / (SIZE * SIZE)},
        "normal": {"minimum_z": min_z, "mean_signed_xy": [mean_x / (SIZE * SIZE), mean_y / (SIZE * SIZE)],
                   "relief_pixels": RELIEF, "flat_encoded_rgb": [128, 128, 255]},
    }
    (ROOT / "Sources/GenerateReport.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
