#!/usr/bin/env python3
"""Run the native LVGL renderer and encode its RGB output as PNG (stdlib only)."""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=root / "build-host")
    parser.add_argument("--output", type=Path, default=root / "build-host/dashboard.png")
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    ppm = build_dir / "dashboard.ppm"
    subprocess.run([str(build_dir / "ui_preview"), str(ppm)], check=True)
    with ppm.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise ValueError("Expected binary RGB PPM")
        width, height = map(int, stream.readline().split())
        if stream.readline() != b"255\n":
            raise ValueError("Expected 8-bit RGB channels")
        pixels = stream.read()
    stride = width * 3
    if len(pixels) != stride * height:
        raise ValueError("Incomplete framebuffer")
    scanlines = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride] for y in range(height))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(scanlines))
           + chunk(b"IEND", b""))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(png)
    print(f"PNG: {args.output.resolve()}")


if __name__ == "__main__":
    main()
