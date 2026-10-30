#!/usr/bin/env python3
"""
Run the Linux release packager over stand-ins and reopen what it writes.

scripts/packaging/linux/package_linux.py (ADR-0012, NOW row 72) gets stand-in
binaries and a stand-in runtime prefix. The test checks the portable archive's
and the .deb's layout: the game folder, the bundled runtime in lib/, the
launchers, and the .deb's control file and members. The .rpm and Arch formats
need rpmbuild and makepkg; the release jobs build those on their own distros.
"""
from __future__ import annotations

import io
import os
import platform
import shlex
import shutil
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/packaging/linux"))
import package_linux  # noqa: E402

ARCH = {"x86_64": "amd64", "aarch64": "arm64", "arm64": "arm64"}.get(platform.machine())
RUNTIME = ("libSDL3.so.0", "libopenal.so.1", "libavformat.so.63", "libavcodec.so.63",
           "libavutil.so.61", "libdxvk_d3d9.so.0")

failures = 0


def check(condition: bool, what: str) -> None:
    global failures
    if not condition:
        failures += 1
        print(f"FAIL: {what}", file=sys.stderr)


def read_ar(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    check(data.startswith(b"!<arch>\n"), "the .deb is an ar archive")
    members, offset = {}, 8
    while offset < len(data):
        header = data[offset:offset + 60]
        name = header[:16].decode().strip()
        size = int(header[48:58].decode().strip())
        members[name] = data[offset + 60:offset + 60 + size]
        offset += 60 + size + (size % 2)
    return members


def member_text(tar: tarfile.TarFile, name: str) -> str:
    """One regular member's text, or empty when it is missing."""
    handle = tar.extractfile(name)
    return handle.read().decode() if handle else ""


def make_inputs(work: Path) -> list[str]:
    """Stand-in binaries, runtime prefix and notices; the packager's arguments."""
    arch = str(ARCH)  # main() returns before this when the machine has no package arch
    # Any ELF of this machine stands in for the client and the server.
    exe = Path(shutil.which("true") or "/bin/true").resolve()
    prefix = work / "prefix"
    (prefix / "lib").mkdir(parents=True)
    for name in RUNTIME:
        (prefix / "lib" / name).write_bytes(b"stand-in " + name.encode())
    # A bundled library that needs a newer glibc than anything else sets the .deb's floor.
    (prefix / "lib" / "libdxvk_d3d9.so.0").write_bytes(b"stand-in\0GLIBC_2.4\0GLIBC_2.99\0GLIBC_PRIVATE\0")
    (prefix / "lib" / "libSDL3.so").symlink_to("libSDL3.so.0")  # a link-time name: not shipped
    notices = work / "notices.txt"
    notices.write_text("stand-in notices\n")
    return ["--client", str(exe), "--dedi", str(exe), "--runtime-prefix", str(prefix),
            "--notices", str(notices), "--version", "0.1.0", "--arch", arch,
            "--out", str(work / "out"), "--formats", "tarxz,deb", "--epoch", "1700000000"]


def check_portable(path: Path) -> None:
    """The portable archive: the game folder with its runtime and launchers."""
    stem = f"kisakcod-0.1.0-linux-{ARCH}"
    with tarfile.open(path) as tar:
        names = set(tar.getnames())
        for name in ("KisakCOD-mp", "KisakCOD-dedi", "kisakcod", "kisakcod-dedi", "README.txt",
                     "LICENSE", "THIRD-PARTY-NOTICES"):
            check(f"{stem}/{name}" in names, f"the portable archive has {name}")
        for name in RUNTIME:
            check(f"{stem}/lib/{name}" in names, f"the portable archive bundles lib/{name}")
        check(f"{stem}/lib/libSDL3.so" not in names, "the link-time libSDL3.so stays out")
        client = tar.getmember(f"{stem}/KisakCOD-mp")
        check(client.mode & 0o111 == 0o111 and client.uid == 0, "the client is executable and root-owned")
        check('dirname "$0"' in member_text(tar, f"{stem}/kisakcod"), "the portable launcher finds its own folder")


def check_deb(path: Path) -> None:
    """The .deb: ar members, control file and installed paths."""
    deb = read_ar(path)
    check(list(deb) == ["debian-binary", "control.tar.xz", "data.tar.xz"], "the .deb members are in order")
    check(deb.get("debian-binary") == b"2.0\n", "the .deb format is 2.0")
    with tarfile.open(fileobj=io.BytesIO(deb["control.tar.xz"])) as tar:
        control = member_text(tar, "./control")
    check(f"Architecture: {ARCH}" in control, "the control file names the arch")
    check("Depends: libc6 (>= 2.99), " in control, "the .deb needs the newest glibc any shipped file needs")
    check("libvulkan1" in control and "libsdl3" not in control.lower(),
          "the .deb depends on the Vulkan loader, not on distro SDL3")
    with tarfile.open(fileobj=io.BytesIO(deb["data.tar.xz"])) as tar:
        data = set(tar.getnames())
        launcher = member_text(tar, "./usr/bin/kisakcod-dedi")
    for name in ("./opt/kisakcod/KisakCOD-mp", "./opt/kisakcod/lib/libdxvk_d3d9.so.0",
                 "./usr/bin/kisakcod", "./usr/share/applications/kisakcod.desktop",
                 "./usr/share/icons/hicolor/scalable/apps/kisakcod.svg"):
        check(name in data, f"the .deb installs {name}")
    check("GAME=/opt/kisakcod" in launcher and "fs_homepath" in launcher,
          "the server launcher keeps its state in the per-user path")


def main() -> int:
    """Package the stand-ins, then check both artifacts."""
    if ARCH is None:
        print(f"skipped: no package arch for {platform.machine()}")
        return 0
    with tempfile.TemporaryDirectory() as work_name:
        work = Path(work_name)
        inputs = make_inputs(work)
        check(package_linux.main(list(inputs)) == 0, "the packager succeeds")
        check_portable(work / "out" / f"kisakcod-0.1.0-linux-{ARCH}.tar.xz")
        check_deb(work / "out" / f"kisakcod_0.1.0_{ARCH}.deb")
        # A relative --out (as release.yml passes) still gives the spec an absolute tree path.
        inputs[inputs.index("--out") + 1] = "relative-out"
        inputs[inputs.index("--formats") + 1] = "rpm"
        here = os.getcwd()
        os.chdir(work)
        try:
            check(package_linux.main(inputs) == 0, "the packager takes a relative --out")
        finally:
            os.chdir(here)
        spec = (work / "relative-out" / "rpm" / "kisakcod.spec").read_text()
        check(f"cp -a {shlex.quote(str(work.resolve() / 'relative-out' / 'rpm' / 'tree'))}/." in spec,
              "the .rpm spec names its staged tree by an absolute path")
    if failures:
        return 1
    print("packaging-linux: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
