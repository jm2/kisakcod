#!/usr/bin/env python3
"""Linux release packages for KisakCOD (ADR-0012, NOW row 72).

Stages the game folder that installs as /opt/kisakcod, then writes any of:

  tarxz  kisakcod-<v>-linux-<arch>.tar.xz   portable; unpack anywhere
  deb    kisakcod_<v>_<debarch>.deb         built here, no dpkg-deb needed
  rpm    kisakcod-<v>-1.<rpmarch>.rpm       rpmbuild over the staged tree
  arch   kisakcod-<v>-1-<pkgarch>.pkg.tar.zst  makepkg over the staged tree

The game folder holds the client, the headless dedicated server, lib/ with
the pinned runtime (SDL3, OpenAL Soft, a Bink-only FFmpeg and dxvk-native from
scripts/platform/linux/runtime), the launchers, the icon, README.txt, LICENSE and
THIRD-PARTY-NOTICES. It never holds retail data: players add theirs to the
per-user path (~/.config/kisakcod) or the game folder.

  package_linux.py --client BIN --dedi BIN --runtime-prefix DIR --notices FILE
      --version V --arch amd64|arm64 --out DIR [--formats tarxz,deb,rpm,arch]
      [--epoch SECONDS]
"""
from __future__ import annotations

import argparse
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
NAME = "kisakcod"
PREFIX = "/opt/kisakcod"
SUMMARY = "Native 64-bit Call of Duty 4 multiplayer client and server"
URL = "https://github.com/jm2/kisakcod"

ARCHES = {
    # ours: (deb, rpm, pacman, ELF machine)
    "amd64": ("amd64", "x86_64", "x86_64", 62),
    "arm64": ("arm64", "aarch64", "aarch64", 183),
}

# Everything else the client needs is in lib/, so a package depends only on
# the host's C/C++ runtime and Vulkan loader (and its GPU driver).
DEB_DEPENDS = "libc6, libstdc++6, libgcc-s1, libatomic1, libvulkan1"
ARCH_DEPENDS = ("glibc", "gcc-libs", "vulkan-icd-loader")
# The bundled runtime's sonames, which rpmbuild must not turn into Requires.
BUNDLED = r"^(libSDL3|libopenal|libavformat|libavcodec|libavutil|libdxvk_d3d9)\\.so.*$"

CLIENT_LAUNCHER = """#!/bin/sh
# KisakCOD client: the game folder is this script's package; user state and,
# optionally, retail data live in ~/.config/kisakcod (ADR-0012).
GAME=@GAME@
cd "$GAME" || exit 1
exec "$GAME/KisakCOD-mp" +set fs_basepath "$GAME" "$@"
"""

DEDI_LAUNCHER = """#!/bin/sh
# KisakCOD dedicated server. The game folder may be read-only, so server.cfg,
# logs and any retail data go in the per-user path (ADR-0012).
GAME=@GAME@
HOME_PATH="${XDG_CONFIG_HOME:-$HOME/.config}/kisakcod"
mkdir -p "$HOME_PATH/main" || exit 1
cd "$GAME" || exit 1
exec "$GAME/KisakCOD-dedi" +set fs_basepath "$GAME" +set fs_homepath "$HOME_PATH" "$@"
"""

DESKTOP = """[Desktop Entry]
Type=Application
Name=KisakCOD
GenericName=Multiplayer shooter
Comment=Native Call of Duty 4 multiplayer client (needs your own game data)
Exec=kisakcod
Icon=kisakcod
Terminal=false
Categories=Game;ActionGame;
"""


def fail(message: str) -> None:
    print(f"::error::{message}", file=sys.stderr)
    raise SystemExit(1)


def elf_machine(path: Path) -> int:
    head = path.read_bytes()[:20]
    if head[:4] != b"\x7fELF":
        fail(f"{path} is not an ELF file")
    return int.from_bytes(head[18:20], "little")


def runtime_libs(prefix: Path) -> list[Path]:
    """The shared libraries (files and soname links) in the runtime prefix."""
    # Versioned names only: the bare libX.so links are for linking, not running.
    libs = sorted(p for p in (prefix / "lib").iterdir() if ".so." in p.name and (p.is_file() or p.is_symlink()))
    for need in ("libSDL3.so.0", "libopenal.so.1", "libdxvk_d3d9.so.0"):
        if not (prefix / "lib" / need).exists():
            fail(f"the runtime prefix has no lib/{need}")
    return libs


def write_text(path: Path, text: str, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(mode)


def readme(version: str, arch: str) -> str:
    return f"""KisakCOD {version} for Linux {arch}
{URL}

A native 64-bit Call of Duty 4 multiplayer client and dedicated server. It
needs your own Steam copy of Call of Duty 4 (1.8): copy or link its main/,
zone/ and localization files into ~/.config/kisakcod (or into this folder).
Nothing from the retail game ships here.

  kisakcod        start the client
  kisakcod-dedi   start the dedicated server (server.cfg and logs live in
                  ~/.config/kisakcod)

Profiles, settings and downloads are kept in ~/.config/kisakcod. The client
renders through dxvk-native and needs a Vulkan driver.

Licenses: LICENSE (GPLv3) and THIRD-PARTY-NOTICES.
"""


def stage(args: argparse.Namespace, root: Path) -> None:
    """The game folder at root."""
    machine = ARCHES[args.arch][3]
    for exe in (args.client, args.dedi):
        if elf_machine(exe) != machine:
            fail(f"{exe} is not a {args.arch} ELF binary")
    root.mkdir(parents=True)
    shutil.copy2(args.client, root / "KisakCOD-mp")
    shutil.copy2(args.dedi, root / "KisakCOD-dedi")
    lib = root / "lib"
    lib.mkdir()
    for so in runtime_libs(args.runtime_prefix):
        shutil.copy2(so, lib / so.name, follow_symlinks=False)
    for exe in (root / "KisakCOD-mp", root / "KisakCOD-dedi"):
        exe.chmod(0o755)
    shutil.copy2(ROOT / "LICENSE", root / "LICENSE")
    shutil.copy2(args.notices, root / "THIRD-PARTY-NOTICES")
    shutil.copy2(HERE / "kisakcod.svg", root / "kisakcod.svg")
    write_text(root / "README.txt", readme(args.version, args.arch))


def launchers(bin_dir: Path, game: str) -> None:
    write_text(bin_dir / "kisakcod", CLIENT_LAUNCHER.replace("@GAME@", game), 0o755)
    write_text(bin_dir / "kisakcod-dedi", DEDI_LAUNCHER.replace("@GAME@", game), 0o755)


def system_tree(args: argparse.Namespace, dest: Path) -> None:
    """The installed file system: /opt/kisakcod plus /usr entry points."""
    stage(args, dest / PREFIX.lstrip("/"))
    launchers(dest / "usr/bin", PREFIX)
    write_text(dest / "usr/share/applications/kisakcod.desktop", DESKTOP)
    icon = dest / "usr/share/icons/hicolor/scalable/apps/kisakcod.svg"
    icon.parent.mkdir(parents=True)
    shutil.copy2(HERE / "kisakcod.svg", icon)


def tar_filter(epoch: int):
    def apply(info: tarfile.TarInfo) -> tarfile.TarInfo:
        info.uid = info.gid = 0
        info.uname = info.gname = "root"
        info.mtime = epoch
        return info
    return apply


def add_tree(tar: tarfile.TarFile, src: Path, arcroot: str, epoch: int) -> None:
    for path in sorted(src.rglob("*")):
        tar.add(path, f"{arcroot}/{path.relative_to(src)}" if arcroot else str(path.relative_to(src)),
                recursive=False, filter=tar_filter(epoch))


def make_tarxz(args: argparse.Namespace, work: Path) -> Path:
    stem = f"{NAME}-{args.version}-linux-{args.arch}"
    root = work / "portable" / stem
    stage(args, root)
    # The portable launchers find the game folder from their own location.
    launchers(root, '"$(cd "$(dirname "$0")" && pwd)"')
    out = args.out / f"{stem}.tar.xz"
    with tarfile.open(out, "w:xz", format=tarfile.GNU_FORMAT) as tar:
        tar.add(root, stem, recursive=False, filter=tar_filter(args.epoch))
        add_tree(tar, root, stem, args.epoch)
    return out


def ar_member(name: str, data: bytes, epoch: int) -> bytes:
    header = f"{name:<16}{epoch:<12}{0:<6}{0:<6}{0o100644:<8o}{len(data):<10}`\n".encode()
    return header + data + (b"\n" if len(data) % 2 else b"")


def make_deb(args: argparse.Namespace, work: Path) -> Path:
    debarch = ARCHES[args.arch][0]
    tree = work / "deb"
    system_tree(args, tree)
    size_kb = sum(p.stat().st_size for p in tree.rglob("*") if p.is_file()) // 1024
    control = (f"Package: {NAME}\nVersion: {args.version}\nArchitecture: {debarch}\n"
               f"Maintainer: KisakCOD Contributors <noreply@github.com>\nInstalled-Size: {size_kb}\n"
               f"Depends: {DEB_DEPENDS}\nSection: games\nPriority: optional\nHomepage: {URL}\n"
               f"Description: {SUMMARY}\n Needs the player's own Steam Call of Duty 4 1.8 data.\n")
    blobs = []
    for label, build in (("control", lambda t: t.addfile(*_mem("./control", control.encode(), args.epoch))),
                         ("data", lambda t: add_tree(t, tree, ".", args.epoch))):
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:xz", format=tarfile.GNU_FORMAT) as tar:
            build(tar)
        blobs.append((f"{label}.tar.xz", buf.getvalue()))
    out = args.out / f"{NAME}_{args.version}_{debarch}.deb"
    with open(out, "wb") as deb:
        deb.write(b"!<arch>\n")
        deb.write(ar_member("debian-binary", b"2.0\n", args.epoch))
        for name, data in blobs:
            deb.write(ar_member(name, data, args.epoch))
    return out


def _mem(name: str, data: bytes, epoch: int):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = 0o644
    info.mtime = epoch
    return tar_filter(epoch)(info), io.BytesIO(data)


def file_list(tree: Path) -> list[str]:
    return sorted("/" + str(p.relative_to(tree)) for p in tree.rglob("*") if not p.is_dir())


def make_rpm(args: argparse.Namespace, work: Path) -> Path:
    rpmarch = ARCHES[args.arch][1]
    top = work / "rpm"
    tree = top / "tree"
    system_tree(args, tree)
    files = "\n".join(f'"{f}"' for f in file_list(tree))
    spec = f"""Name: {NAME}
Version: {args.version.replace('-', '~')}
Release: 1
Summary: {SUMMARY}
License: GPL-3.0-only
URL: {URL}
BuildArch: {rpmarch}
# dxvk-native dlopen()s the loader, which rpmbuild cannot see.
Requires: vulkan-loader
%global __brp_strip %{{nil}}
%global debug_package %{{nil}}
%global __requires_exclude {BUNDLED}
%global __provides_exclude_from ^{PREFIX}/lib/.*$
%description
{SUMMARY}. Needs the player's own Steam Call of Duty 4 1.8 data.
%install
cp -a {tree}/. %{{buildroot}}/
%files
%dir {PREFIX}
%dir {PREFIX}/lib
{files}
"""
    (top / "kisakcod.spec").write_text(spec)
    subprocess.run(["rpmbuild", "-bb", "--target", rpmarch, "--define", f"_topdir {top}",
                    "--define", f"source_date_epoch_from_changelog 0",
                    str(top / "kisakcod.spec")],
                   check=True, env=dict(os.environ, SOURCE_DATE_EPOCH=str(args.epoch)))
    built = list((top / "RPMS" / rpmarch).glob("*.rpm"))
    if len(built) != 1:
        fail(f"rpmbuild made {len(built)} packages")
    out = args.out / built[0].name
    shutil.move(built[0], out)
    return out


def make_arch(args: argparse.Namespace, work: Path) -> Path:
    pkgarch = ARCHES[args.arch][2]
    top = work / "arch"
    tree = top / "tree"
    system_tree(args, tree)
    depends = " ".join(f"'{d}'" for d in ARCH_DEPENDS)
    pkgver = args.version.replace("-", "_")
    (top / "PKGBUILD").write_text(f"""pkgname={NAME}
pkgver={pkgver}
pkgrel=1
pkgdesc='{SUMMARY}'
arch=('{pkgarch}')
url='{URL}'
license=('GPL-3.0-only')
depends=({depends})
options=('!strip' '!debug')
package() {{
  cp -a '{tree}'/. "$pkgdir"/
}}
""")
    env = dict(os.environ, PKGDEST=str(args.out), SOURCE_DATE_EPOCH=str(args.epoch), CARCH=pkgarch,
               PKGEXT=".pkg.tar.zst")
    subprocess.run(["makepkg", "--force", "--nodeps", "--noconfirm"], cwd=top, check=True, env=env)
    built = list(args.out.glob(f"{NAME}-{pkgver}-1-{pkgarch}.pkg.tar.*"))
    if len(built) != 1:
        fail(f"makepkg made {len(built)} packages")
    return built[0]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--dedi", type=Path, required=True)
    parser.add_argument("--runtime-prefix", type=Path, required=True,
                        help="the scripts/platform/linux/runtime install prefix")
    parser.add_argument("--notices", type=Path, required=True, help="THIRD-PARTY-NOTICES to ship")
    parser.add_argument("--version", required=True)
    parser.add_argument("--arch", choices=sorted(ARCHES), required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--formats", default="tarxz,deb,rpm,arch")
    parser.add_argument("--epoch", type=int, default=int(os.environ.get("SOURCE_DATE_EPOCH", "0")))
    args = parser.parse_args(argv)
    makers = {"tarxz": make_tarxz, "deb": make_deb, "rpm": make_rpm, "arch": make_arch}
    formats = [f for f in args.formats.split(",") if f]
    unknown = [f for f in formats if f not in makers]
    if unknown:
        fail(f"unknown formats: {', '.join(unknown)}")
    args.out.mkdir(parents=True, exist_ok=True)
    # Large scratch: honour $TMPDIR, else /var/tmp rather than a RAM /tmp.
    with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR") or "/var/tmp") as work:
        for fmt in formats:
            print(makers[fmt](args, Path(work)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
