#!/usr/bin/env python3
"""
Build kisakcod.ico and kisakcod.icns from kisakcod.svg.

  make_icons.py [--out DIR]

kisakcod.svg is the only source. The Windows .ico (resource 1 of every
executable, scripts/platform/windows/kisakcod.rc, and the installer's icon)
holds the full-bleed artwork at 16-256 px. The macOS .icns places the same
artwork on Apple's icon grid: a continuous-corner rounded square of 824 units
centred in 1024, with the grid's drop shadow in the margin. An icon with
that shape keeps it in Finder and the Dock instead of being set on a plate.
Linux installs kisakcod.svg as is.

Rendering needs rsvg-convert, or ImageMagick built with librsvg, and Pillow.
Rerun this after editing the SVG and commit the .ico and .icns it writes.
"""
from __future__ import annotations

import argparse
import io
import re
import shutil
import struct
import subprocess  # nosec B404: runs the SVG renderer with fixed arguments
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 256)
# OSType -> pixel size; every entry holds PNG data (macOS 10.7 and later).
ICNS_TYPES = (("icp4", 16), ("icp5", 32), ("ic11", 32), ("icp6", 64), ("ic12", 64), ("ic07", 128),
              ("ic08", 256), ("ic13", 256), ("ic09", 512), ("ic14", 512), ("ic10", 1024))
# Apple's macOS icon grid, in units of the 1024 canvas.
MAC_BODY = 824
MAC_RADIUS = 185.4


def continuous_rect(x: float, y: float, size: float, r: float) -> str:
    """A square with continuous (squircle) corners, as an SVG path.

    The corner is the curve UIKit and AppKit draw for a continuous corner of
    radius r: two Bezier segments joined by a short line, easing in over
    1.528 r instead of a circular arc's r.
    """
    # Points of one corner, (along the edge, into the corner), from the top
    # edge round to the right edge, in units of r measured from the corner.
    corner = ((1.52866483, 0), (1.08849299, 0), (0.86840701, 0), (0.66993427, 0.06549600),
              (0.63149399, 0.07491100), (0.37282392, 0.16905899), (0.16905899, 0.37282392),
              (0.07491100, 0.63149399), (0.06549600, 0.66993427), (0, 0.86840701), (0, 1.08849299),
              (0, 1.52866483))
    x2, y2 = x + size, y + size
    # Each corner maps (a, b) to a point; the four corners turn clockwise.
    maps = (lambda a, b: (x2 - a * r, y + b * r),
            lambda a, b: (x2 - b * r, y2 - a * r),
            lambda a, b: (x + a * r, y2 - b * r),
            lambda a, b: (x + b * r, y + a * r))
    fmt = lambda p: f"{p[0]:.3f} {p[1]:.3f}"  # noqa: E731
    d = [f"M{fmt(maps[3](*corner[-1]))}"]
    for m in maps:
        p = [m(a, b) for a, b in corner]
        d += [f"L{fmt(p[0])}", f"C{fmt(p[1])} {fmt(p[2])} {fmt(p[3])}", f"L{fmt(p[4])}",
              f"C{fmt(p[5])} {fmt(p[6])} {fmt(p[7])}", f"L{fmt(p[8])}",
              f"C{fmt(p[9])} {fmt(p[10])} {fmt(p[11])}"]
    return " ".join(d) + "Z"


def macos_variant(svg: bytes) -> bytes:
    """Place the artwork on Apple's grid: squircle shape, fitted art, shadow.

    kisakcod.svg marks what changes with ids, so plain substitution does it:
    #shape and #outline take the grid's squircle, #art is fitted to it, and
    a shadow path goes in under the clipped art.
    """
    text = svg.decode()
    margin = (1024 - MAC_BODY) / 2
    shape = continuous_rect(margin, margin, MAC_BODY, MAC_RADIUS)

    def one(pattern: str, repl: str, what: str) -> None:
        nonlocal text
        text, n = re.subn(pattern, repl, text)
        if n != 1:
            raise SystemExit(f"make_icons: kisakcod.svg has {n} {what}, not 1")

    for name in ("shape", "outline"):
        one(rf'(<path id="{name}" d=")[^"]*(")', rf"\g<1>{shape}\g<2>", f"#{name} paths")
    # The full-bleed art fills 32-992; fit that box to the grid's body.
    scale = MAC_BODY / 960
    one(r'<g id="art">', f'<g id="art" transform="translate(512 512) scale({scale:.6f}) translate(-512 -512)">',
        "#art groups")
    one(r"</defs>", '<filter id="mac-shadow" x="-10%" y="-10%" width="120%" height="120%">'
        '<feDropShadow dx="0" dy="10" stdDeviation="10" flood-color="#000" flood-opacity=".3"/></filter>\n'
        f'  </defs>\n  <path d="{shape}" fill="#000" filter="url(#mac-shadow)"/>', "</defs> tags")
    return text.encode()


def renderer() -> list[str]:
    for tool, args in (("rsvg-convert", ["-w", "{size}", "-h", "{size}", "-o", "{out}", "{svg}"]),
                       ("magick", ["-background", "none", "{svg}", "-resize", "{size}x{size}", "{out}"])):
        path = shutil.which(tool)
        if path:
            return [path, *args]
    raise SystemExit("make_icons: needs rsvg-convert or ImageMagick (magick) with librsvg")


def render(svg: bytes, size: int, command: list[str], work: Path) -> bytes:
    """Render svg at size x size as a PNG with no timestamps."""
    from PIL import Image  # only rendering needs Pillow

    src, out = work / "icon.svg", work / "icon.png"
    src.write_bytes(svg)
    args = [a.format(size=size, out=out, svg=src) for a in command]
    # A renderer found by renderer() and our own temp paths; no shell, no user input.
    subprocess.run(args, check=True)  # nosec B603  # nosemgrep
    with Image.open(out) as image:
        image = image.convert("RGBA")
        if image.size != (size, size):
            raise SystemExit(f"make_icons: the renderer gave {image.size} for {size} px")
        buf = io.BytesIO()
        image.save(buf, "PNG", optimize=True)  # Pillow writes no tIME chunk
    return buf.getvalue()


def ico(pngs: dict[int, bytes]) -> bytes:
    """An .ico whose every image is PNG-compressed (Windows Vista and later)."""
    sizes = sorted(pngs)
    head = struct.pack("<HHH", 0, 1, len(sizes))
    entries, data = b"", b""
    offset = len(head) + 16 * len(sizes)
    for size in sizes:
        png = pngs[size]
        dim = size % 256  # 0 means 256
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset + len(data))
        data += png
    return head + entries + data


def icns(pngs: dict[int, bytes]) -> bytes:
    body = b"".join(t.encode() + struct.pack(">I", 8 + len(pngs[s])) + pngs[s] for t, s in ICNS_TYPES)
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--svg", type=Path, default=HERE / "kisakcod.svg")
    parser.add_argument("--out", type=Path, default=HERE)
    args = parser.parse_args(argv)
    svg = args.svg.read_bytes()
    command = renderer()
    with tempfile.TemporaryDirectory(dir=args.out) as tmp:
        work = Path(tmp)
        full = {s: render(svg, s, command, work) for s in ICO_SIZES}
        mac_svg = macos_variant(svg)
        mac = {s: render(mac_svg, s, command, work) for s in sorted({s for _, s in ICNS_TYPES})}
    (args.out / "kisakcod.ico").write_bytes(ico(full))
    (args.out / "kisakcod.icns").write_bytes(icns(mac))
    print(f"make_icons: wrote {args.out / 'kisakcod.ico'} and {args.out / 'kisakcod.icns'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
