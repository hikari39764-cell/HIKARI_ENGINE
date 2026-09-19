"""Generate periodic RG vector fields; cook with --only for each declared ID."""
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2] / 'HIKARI/EngineContent'
SIZE = 128

def field(name, u, v):
    tau = math.tau
    if name == 'flow_stream':
        # 平均方向 + 周期的な蛇行。縫い目でも速度が連続する。
        return 0.72 + 0.20 * math.sin(tau*v), 0.24 * math.sin(tau*u + 0.7*math.sin(tau*v))
    # 周期ポテンシャルの回転勾配。格子状の渦を作る無発散場。
    return math.sin(tau*u)*math.cos(tau*v)*0.85, -math.cos(tau*u)*math.sin(tau*v)*0.85

def generate(name):
    data = bytearray(struct.pack('<BBBHHBHHHHBB', 0,0,2,0,0,0,0,0,SIZE,SIZE,32,0x28))
    for y in range(SIZE):
        for x in range(SIZE):
            r, g = field(name, (x+0.5)/SIZE, (y+0.5)/SIZE)
            encode = lambda value: round(max(0, min(1, value*0.5+0.5))*255)
            data.extend((128, encode(g), encode(r), 255))
    path = ROOT / f'Vfx/Textures/{name}.tga'
    path.write_bytes(data)
    return hashlib.sha256(data).hexdigest().upper()

if __name__ == '__main__':
    manifest = ROOT / 'Vfx/BuiltinAssets.json'
    contents = json.loads(manifest.read_text(encoding='utf-8-sig'))
    for name in ['flow_stream', 'flow_vortex']:
        identity = 'engine.vfx.texture.' + name
        asset = next((a for a in contents['assets'] if a['id'] == identity), None)
        if asset is None:
            asset = {'id': identity, 'type': 'Texture', 'path': f'Vfx/Textures/Cooked/{name}.htex',
                     'sourcePath': f'Vfx/Textures/{name}.tga', 'colorSpace': 'Linear', 'usage': 'Mask',
                     'compression': 'BC7', 'mipPolicy': 'Generate', 'sourceId': 'hikari.generated',
                     'upstreamPath': '', 'sha256': ''}
            contents['assets'].append(asset)
        asset['usage'] = 'Mask'
        asset['sourceSha256'] = generate(name)
    manifest.write_text(json.dumps(contents, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
