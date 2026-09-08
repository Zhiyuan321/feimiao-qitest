#!/usr/bin/env python3
"""Lossless re-container of existing ICNS PNG assets into a Windows ICO."""
import struct
from pathlib import Path
root = Path(__file__).resolve().parent.parent
source = (root / "resources/brand/qitest-app.icns").read_bytes()
if source[:4] != b"icns":
    raise ValueError("Not ICNS")
images = {}
position = 8
while position + 8 <= len(source):
    length = struct.unpack(">I", source[position + 4:position + 8])[0]
    if length < 8 or position + length > len(source):
        raise ValueError("Bad ICNS chunk")
    png = source[position + 8:position + length]
    if png.startswith(b"\x89PNG\r\n\x1a\n"):
        width, height = struct.unpack(">II", png[16:24])
        if width == height and width <= 256:
            images[width] = png
    position += length
if not images:
    raise ValueError("ICNS has no Windows-compatible PNG representation")
offset = 6 + 16 * len(images)
header = struct.pack("<HHH", 0, 1, len(images))
payload = b""
for size, png in sorted(images.items()):
    header += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset)
    payload += png
    offset += len(png)
target = root / "resources/brand/qitest-app.ico"
target.write_bytes(header + payload)
print(f"{target.name}: {sorted(images)} pixels")
