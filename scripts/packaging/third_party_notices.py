#!/usr/bin/env python3
"""Write THIRD-PARTY-NOTICES for a release package (ADR-0012, #263).

The engine's own third-party code is listed here, so every target ships the
same text for it. A package adds the components it bundles with --licenses
(one directory per component, as the macOS runtime installs them) or
--component NAME=FILE. A missing or empty license text is an error.

Usage:
  third_party_notices.py --out FILE [--client] [--licenses DIR]... [--component NAME=FILE]...

--client adds the code only the client links (Speex, for voice chat).
"""

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

# (name, what it is, license files relative to the repository, client only)
ENGINE = (
    ("Open Dynamics Engine", "physics, used under its BSD license option",
     ("src/physics/ode/LICENSE-BSD.TXT",), False),
    ("zlib 1.3.1", "compression", ("deps/zlib/LICENSE",), False),
    ("unzip 0.15 by Gilles Vollant", "zip archive reading, src/qcommon/unzip.cpp",
     ("src/qcommon/unzip-LICENSE.txt",), False),
    ("Speex 1.1.9", "voice chat codec",
     ("src/groupvoice/speex/COPYING", "src/groupvoice/speex/NOTICE"), True),
)

HEADER = """\
KisakCOD bundles or is built from the third-party components below. KisakCOD
itself is licensed under the GNU General Public License version 3 (LICENSE).
Portions of the engine derive from the Quake III Arena source code, Copyright
(C) 1999-2005 Id Software, Inc., under the GNU General Public License version
2 or (at your option) any later version, and are used here under version 3.
Each component's own license text follows its heading.
"""


class Missing(Exception):
    pass


def read_text(path):
    try:
        text = Path(path).read_text(encoding="utf-8", errors="replace").strip()
    except OSError as error:
        raise Missing(f"cannot read license text {path}: {error.strerror}") from error
    if not text:
        raise Missing(f"empty license text {path}")
    return text


def engine_sections(client):
    return [(f"{name} ({role})", [read_text(ROOT / relative) for relative in files])
            for name, role, files, client_only in ENGINE if client or not client_only]


def directory_sections(directory):
    directory = Path(directory)
    if not directory.is_dir():
        raise Missing(f"license directory {directory} does not exist")
    sections = []
    for component in sorted(p for p in directory.iterdir() if p.is_dir()):
        files = sorted(p for p in component.rglob("*") if p.is_file())
        if not files:
            raise Missing(f"no license text for {component.name}")
        sections.append((component.name, [read_text(f) for f in files]))
    return sections


def component_section(spec):
    name, separator, path = spec.partition("=")
    if not separator or not name or not path:
        raise Missing(f"--component wants NAME=FILE, got {spec!r}")
    return (name, [read_text(path)])


def collect(client, license_dirs, components):
    sections = engine_sections(client)
    for directory in license_dirs:
        sections += directory_sections(directory)
    sections += [component_section(spec) for spec in components]
    return sections


def render(sections):
    out = [HEADER]
    for title, texts in sections:
        out.append("=" * 78 + "\n" + title + "\n" + "=" * 78 + "\n")
        out.extend(text + "\n" for text in texts)
    return "\n".join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write THIRD-PARTY-NOTICES for a release package.")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--client", action="store_true")
    parser.add_argument("--licenses", action="append", default=[])
    parser.add_argument("--component", action="append", default=[])
    args = parser.parse_args(argv)
    try:
        sections = collect(args.client, args.licenses, args.component)
    except Missing as error:
        print(f"third_party_notices: {error}", file=sys.stderr)
        return 1
    args.out.write_text(render(sections), encoding="utf-8")
    print(f"third_party_notices: {args.out} ({len(sections)} components)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
