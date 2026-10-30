#!/usr/bin/env python3
"""
Run the icon builder's shape and container code.

scripts/packaging/icons/make_icons.py turns kisakcod.svg into the Windows
.ico and the macOS .icns. The test builds the macOS variant of the real SVG
and checks it sits on Apple's grid, then writes an .ico and an .icns from
stand-in images and reads their directories back. Rendering needs a
rasterizer and is not run here.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/packaging/icons"))
import make_icons  # noqa: E402

FAILURES = []


def check(ok: bool, what: str) -> None:
    if not ok:
        FAILURES.append(what)


def points(d: str) -> list[tuple[float, float]]:
    nums = [float(n) for n in re.findall(r"-?\d+(?:\.\d+)?", d)]
    return list(zip(nums[::2], nums[1::2]))


def test_continuous_rect() -> None:
    d = make_icons.continuous_rect(100, 100, 824, 185.4)
    check(d.startswith("M") and d.endswith("Z"), "the squircle is not a closed path")
    pts = points(d)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    check(abs(min(xs) - 100) < 1e-6 and abs(max(xs) - 924) < 1e-6, f"x spans {min(xs)}..{max(xs)}")
    check(abs(min(ys) - 100) < 1e-6 and abs(max(ys) - 924) < 1e-6, f"y spans {min(ys)}..{max(ys)}")
    # The straight part of each edge starts 1.528 r from the corner.
    check(abs(pts[0][0] - (100 + 1.52866483 * 185.4)) < 1e-3, f"the top edge starts at {pts[0]}")
    # Symmetric under a quarter turn about the centre (as a set: the start
    # point appears twice, once for the moveto and once closing the path).
    turned = {(round(1024 - y, 2), round(x, 2)) for x, y in pts}
    check(turned == {(round(x, 2), round(y, 2)) for x, y in pts}, "the corners differ")


def test_macos_variant() -> None:
    svg = (ROOT / "scripts/packaging/icons/kisakcod.svg").read_bytes()
    out = make_icons.macos_variant(svg).decode()
    shape = make_icons.continuous_rect(100, 100, 824, 185.4)
    for name in ("shape", "outline"):
        found = re.search(rf'<path id="{name}" d="([^"]*)"', out)
        check(found is not None and found.group(1) == shape, f"#{name} is not the grid's squircle")
    art = re.search(r'<g id="art" transform="([^"]*)"', out)
    check(art is not None and "scale(0.858333)" in art.group(1), "the art is not fitted to 824")
    check(out.count(f'<path d="{shape}" fill="#000" filter="url(#mac-shadow)"/>') == 1,
          "no drop shadow under the squircle")
    check(out.index('<filter id="mac-shadow"') < out.index("</defs>") < out.index('filter="url(#mac-shadow)"/>'),
          "the shadow filter is not defined before its use")
    # The shadow sits under the clipped art, which sits under the outline.
    check(out.index("url(#mac-shadow)\"/>") < out.index('clip-path="url(#clip)">') < out.index('id="outline"'),
          "the layers are out of order")


def test_ico_directory() -> None:
    pngs = {s: f"png{s}".encode() for s in make_icons.ICO_SIZES}
    data = make_icons.ico(pngs)
    reserved, kind, count = struct.unpack_from("<HHH", data)
    check((reserved, kind, count) == (0, 1, len(pngs)), f"ico header {reserved, kind, count}")
    for i, size in enumerate(sorted(pngs)):
        w, h, _, _, planes, bpp, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        check((w or 256, h or 256, planes, bpp) == (size, size, 1, 32), f"ico entry {i}: {w, h, planes, bpp}")
        check(data[offset:offset + length] == pngs[size], f"ico entry {i} points at the wrong image")


def test_icns_directory() -> None:
    sizes = {s for _, s in make_icons.ICNS_TYPES}
    pngs = {s: f"png{s}".encode() for s in sizes}
    data = make_icons.icns(pngs)
    check(data[:4] == b"icns" and struct.unpack_from(">I", data, 4)[0] == len(data), "icns header")
    seen, offset = [], 8
    while offset < len(data):
        kind, length = data[offset:offset + 4].decode(), struct.unpack_from(">I", data, offset + 4)[0]
        seen.append((kind, data[offset + 8:offset + length]))
        offset += length
    check(offset == len(data), "icns entries overrun the file")
    check(seen == [(t, pngs[s]) for t, s in make_icons.ICNS_TYPES], f"icns entries {[k for k, _ in seen]}")
    check(("ic10", pngs[1024]) in seen, "no 1024 px (512@2x) entry")


def main() -> int:
    for test in (test_continuous_rect, test_macos_variant, test_ico_directory, test_icns_directory):
        test()
    for failure in FAILURES:
        print(f"packaging-icons: FAIL {failure}")
    if FAILURES:
        return 1
    print("packaging-icons: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
