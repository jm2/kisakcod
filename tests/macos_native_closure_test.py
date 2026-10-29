#!/usr/bin/env python3
"""scripts/platform/macos/native_closure.py on synthetic and real Mach-O apps.

The synthetic fixtures run on any host; the real-linker case runs on macOS.
Trimmed from jm2/balun scripts/test_macos_native_closure.py.
"""

import plistlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/platform/macos"))
import native_closure as closure  # noqa: E402

ARM64 = 0x100000C
X86_64 = 0x1000007


def command(kind, value):
    prefix = 24 if kind in closure.LOAD_DYLIB | {0xD} else 12
    payload = value.encode() + b"\0"
    length = (prefix + len(payload) + 7) & ~7
    return (struct.pack("<III", kind, length, prefix) + bytes(prefix - 12) + payload
            + bytes(length - prefix - len(payload)))


def build_version(major, minor=0):
    return struct.pack("<IIIIII", 0x32, 24, 1, major << 16 | minor << 8, 0, 0)


def macho(imports=(), rpaths=(), *, cpu=ARM64, kind=6, minimum=26):
    commands = [struct.pack("<II", 0x1B, 24) + bytes(16)]
    commands += [command(0xC, value) for value in imports]
    commands += [command(0x8000001C, value) for value in rpaths]
    commands.append(build_version(minimum))
    data = b"".join(commands)
    subtype = 3 if cpu == X86_64 else 0
    return struct.pack("<IIIIIIII", 0xFEEDFACF, cpu, subtype, kind,
                       len(commands), len(data), 0, 0) + data


class Fixture(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.parent = Path(temporary.name)
        self.app = self.parent / "Kisak With Spaces.app"
        self.exe = self.write("Contents/MacOS/KisakCOD-mp", macho(kind=2))
        self.write("Contents/Info.plist", plistlib.dumps({"LSMinimumSystemVersion": "26.0"}))

    def write(self, name, data):
        target = self.app / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return target

    def client(self, imports, rpaths=("@executable_path/../Frameworks", "@loader_path")):
        self.exe.write_bytes(macho(list(imports) + ["/usr/lib/libSystem.B.dylib"], rpaths, kind=2))

    def rejects(self, pattern):
        with self.assertRaisesRegex(closure.Invalid, pattern):
            closure.validate(self.app)
            closure.check_minimum(self.app)


class Closure(Fixture):
    def test_bundle_shaped_like_the_client_passes(self):
        # The client links dxvk and SDL3; the Vulkan driver is only dlopened.
        self.client(["@rpath/libdxvk_d3d9.0.dylib", "@rpath/libSDL3.0.dylib"])
        self.write("Contents/Frameworks/libdxvk_d3d9.0.dylib",
                   macho(["@rpath/libSDL3.0.dylib"], ["@loader_path"]))
        self.write("Contents/Frameworks/libSDL3.0.dylib", macho(rpaths=["@loader_path"]))
        self.write("Contents/Frameworks/libvulkan_kosmickrisp.dylib",
                   macho(["@rpath/libzstd.1.dylib",
                          "/System/Library/Frameworks/Metal.framework/Versions/A/Metal"],
                         ["@loader_path"]))
        self.write("Contents/Frameworks/libzstd.1.dylib", macho())
        self.assertEqual(closure.validate(self.app), 5)
        self.assertEqual(closure.check_minimum(self.app), (26, 0, 0))

    def test_homebrew_install_name_is_rejected(self):
        self.client(["/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib"])
        self.rejects("absolute non-system")

    def test_build_host_run_path_is_rejected(self):
        # CMake's build RPATH to the runtime prefix, left in the client.
        prefix = self.parent / "build-runtime/prefix/lib"
        prefix.mkdir(parents=True)
        (prefix / "libSDL3.0.dylib").write_bytes(macho())
        self.write("Contents/Frameworks/libSDL3.0.dylib", macho())
        self.client(["@rpath/libSDL3.0.dylib"],
                    [str(prefix), "@executable_path/../Frameworks"])
        self.rejects("escapes app")

    def test_missing_framework_is_rejected(self):
        self.client(["@rpath/libopenal.1.dylib"])
        self.rejects("unresolved @rpath")

    def test_dlopened_driver_dependencies_are_checked(self):
        self.client([])
        self.write("Contents/Frameworks/libvulkan_kosmickrisp.dylib",
                   macho(["/opt/homebrew/opt/zstd/lib/libzstd.1.dylib"]))
        self.rejects("absolute non-system")

    def test_framework_newer_than_info_plist_is_rejected(self):
        # A 27.0 framework from a stale build directory.
        self.client(["@rpath/libzstd.1.dylib"])
        self.write("Contents/Frameworks/libzstd.1.dylib", macho(minimum=27))
        self.rejects("requires macOS 27.0, above LSMinimumSystemVersion 26.0")

    def test_wrong_architecture_is_rejected(self):
        self.client(["@rpath/libzstd.1.dylib"])
        self.write("Contents/Frameworks/libzstd.1.dylib", macho(cpu=X86_64))
        self.rejects("compatible (architecture|executable context)")

    def test_truncated_image_fails_closed(self):
        self.exe.write_bytes(macho(kind=2)[:40])
        self.rejects(".")


@unittest.skipUnless(sys.platform == "darwin", "needs the Apple linker")
class RealLinker(Fixture):
    def test_clang_output_and_its_run_paths(self):
        source = self.parent / "main.c"
        source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
        link = ["clang", "-mmacosx-version-min=26.0", str(source), "-o", str(self.exe)]
        subprocess.run(link, check=True, capture_output=True)
        self.assertEqual(closure.validate(self.app), 1)
        self.assertEqual(closure.check_minimum(self.app), (26, 0, 0))
        subprocess.run(link + ["-Wl,-rpath," + str(self.parent)], check=True, capture_output=True)
        self.assertIn(str(self.parent), closure.inspect(self.exe)[0].rpaths)


if __name__ == "__main__":
    unittest.main()
