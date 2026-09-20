#!/usr/bin/env python3
"""Build assets/Dawn_app.ico from assets/Dawn_app.png."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PNG = ROOT / "assets" / "Dawn_app.png"
ICO = ROOT / "assets" / "Dawn_app.ico"
SIZES = (16, 24, 32, 48, 64, 128, 256)


def load_png_rgba(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"not a png: {path}")
    width = height = 0
    raw = bytearray()
    off = 8
    while off + 12 <= len(data):
        length, ctype = struct.unpack_from(">I4s", data, off)
        chunk = data[off + 8 : off + 8 + length]
        off += 12 + length
        if ctype == b"IHDR":
            width, height, bit, color, comp, filt, inter = struct.unpack(">IIBBBBB", chunk)
            if bit != 8 or color != 6 or comp != 0 or filt != 0 or inter != 0:
                raise SystemExit("png must be 8-bit RGBA, no interlace")
        elif ctype == b"IDAT":
            raw.extend(chunk)
        elif ctype == b"IEND":
            break
    pixels = zlib.decompress(bytes(raw))
    stride = width * 4
    row_bytes = stride + 1
    out = bytearray(width * height * 4)
    prev = bytearray(stride)
    src = 0
    for y in range(height):
        filt = pixels[src]
        row = bytearray(pixels[src + 1 : src + 1 + stride])
        src += row_bytes
        if filt == 1:
            for i in range(4, stride):
                row[i] = (row[i] + row[i - 4]) & 255
        elif filt == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 255
        elif filt == 3:
            for i in range(stride):
                left = row[i - 4] if i >= 4 else 0
                row[i] = (row[i] + ((left + prev[i]) // 2)) & 255
        elif filt == 4:
            for i in range(stride):
                a = row[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else b if pb <= pc else c
                row[i] = (row[i] + pr) & 255
        elif filt != 0:
            raise SystemExit(f"unsupported png filter {filt}")
        out[y * stride : (y + 1) * stride] = row
        prev = row
    return width, height, bytes(out)


def scale_rgba(src: bytes, sw: int, sh: int, dw: int, dh: int) -> bytes:
    if sw == dw and sh == dh:
        return src
    out = bytearray(dw * dh * 4)
    for y in range(dh):
        y0 = y * sh // dh
        y1 = max(y0 + 1, (y + 1) * sh // dh)
        for x in range(dw):
            x0 = x * sw // dw
            x1 = max(x0 + 1, (x + 1) * sw // dw)
            r = g = b = a = n = 0
            for yy in range(y0, y1):
                row = yy * sw * 4
                for xx in range(x0, x1):
                    i = row + xx * 4
                    r += src[i]
                    g += src[i + 1]
                    b += src[i + 2]
                    a += src[i + 3]
                    n += 1
            o = (y * dw + x) * 4
            out[o] = r // n
            out[o + 1] = g // n
            out[o + 2] = b // n
            out[o + 3] = a // n
    return bytes(out)


def dib32(rgba: bytes, w: int, h: int) -> bytes:
    xor = bytearray(w * h * 4)
    for y in range(h):
        src = (h - 1 - y) * w * 4
        dst = y * w * 4
        for x in range(w):
            i = src + x * 4
            o = dst + x * 4
            xor[o] = rgba[i + 2]
            xor[o + 1] = rgba[i + 1]
            xor[o + 2] = rgba[i]
            xor[o + 3] = rgba[i + 3]
    and_row = ((w + 31) // 32) * 4
    mask = bytes(and_row * h)
    header = struct.pack(
        "<IIIHHIIIIII",
        40,
        w,
        h * 2,
        1,
        32,
        0,
        len(xor),
        0,
        0,
        0,
        0,
    )
    return header + xor + mask


def write_ico(path: Path, images: list[tuple[int, bytes]]) -> None:
    count = len(images)
    offset = 6 + 16 * count
    chunks = [struct.pack("<HHH", 0, 1, count)]
    blobs = []
    for size, blob in images:
        chunks.append(
            struct.pack(
                "<BBBBHHII",
                size if size < 256 else 0,
                size if size < 256 else 0,
                0,
                0,
                1,
                32,
                len(blob),
                offset,
            )
        )
        blobs.append(blob)
        offset += len(blob)
    path.write_bytes(b"".join(chunks) + b"".join(blobs))


def main() -> None:
    width, height, rgba = load_png_rgba(PNG)
    images = []
    for size in SIZES:
        scaled = scale_rgba(rgba, width, height, size, size)
        images.append((size, dib32(scaled, size, size)))
    write_ico(ICO, images)
    print(f"wrote {ICO} ({ICO.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
