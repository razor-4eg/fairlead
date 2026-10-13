#!/usr/bin/env python3
"""Draw the Fairlead icon: full-bleed plate, antialiased ring and dot."""

import struct
import subprocess
import zlib
from pathlib import Path


def clamp(value: float) -> float:
    return 0.0 if value < 0 else 1.0 if value > 1 else value


def png(size: int) -> bytes:
    background = (11, 15, 14)
    mark = (124, 219, 182)
    center = (size - 1) / 2
    radius = size * 0.275
    stroke = max(size * 0.055, 2.0)
    dot_r = size * 0.048
    dot_x = center + size * 0.105
    dot_y = center - size * 0.038
    samples = 4
    raw = bytearray()

    def coverage(px: float, py: float) -> float:
        dx = px - center
        dy = py - center
        ring = abs((dx * dx + dy * dy) ** 0.5 - radius) - stroke / 2
        ddx = px - dot_x
        ddy = py - dot_y
        dot = (ddx * ddx + ddy * ddy) ** 0.5 - dot_r
        ring_c = clamp(0.5 - ring)
        dot_c = clamp(0.5 - dot)
        return ring_c if ring_c > dot_c else dot_c

    for y in range(size):
        raw.append(0)
        for x in range(size):
            cover = 0.0
            for sy in range(samples):
                for sx in range(samples):
                    px = x + (sx + 0.5) / samples
                    py = y + (sy + 0.5) / samples
                    cover += coverage(px, py)
            cover /= samples * samples
            red = int(background[0] * (1 - cover) + mark[0] * cover + 0.5)
            green = int(background[1] * (1 - cover) + mark[1] * cover + 0.5)
            blue = int(background[2] * (1 - cover) + mark[2] * cover + 0.5)
            raw.extend((red, green, blue, 255))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    iconset = root / "Support" / "AppIcon.iconset"
    iconset.mkdir(parents=True, exist_ok=True)
    sizes = {
        "icon_16x16.png": 16,
        "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32,
        "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128,
        "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256,
        "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512,
        "icon_512x512@2x.png": 1024,
    }
    cache = {}
    for name, size in sizes.items():
        if size not in cache:
            cache[size] = png(size)
        (iconset / name).write_bytes(cache[size])
    output = root / "Support" / "AppIcon.icns"
    subprocess.check_call(["iconutil", "-c", "icns", str(iconset), "-o", str(output)])
    print(output)


if __name__ == "__main__":
    main()
