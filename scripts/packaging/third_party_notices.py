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

# (name, what it is, license file relative to the repository, client only)
ENGINE = (
    ("Open Dynamics Engine", "physics, used under its BSD license option",
     "src/physics/ode/LICENSE-BSD.TXT", False),
    ("zlib", "compression", "deps/zlib/LICENSE", False),
    ("Speex", "voice chat codec", "src/groupvoice/speex/COPYING", True),
)

HEADER = """\
KisakCOD bundles or is built from the third-party components below. KisakCOD
itself is licensed under the GNU General Public License version 3 (LICENSE).
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
    return [(f"{name} ({role})", [read_text(ROOT / relative)])
            for name, role, relative, client_only in ENGINE if client or not client_only]


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
