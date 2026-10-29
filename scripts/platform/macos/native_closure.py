#!/usr/bin/env python3
# From jm2/balun scripts/macos_native_closure.py at 3e0de3dd (GPL-3.0, the
# same owner and license). Changed only for Codacy: ExpatError comes from
# pyexpat, a loop variable no longer shadows main(), and two require() calls
# are explicit raises so None narrows. bundle.sh runs
# `native_closure.py bundle <app>` on the assembled KisakCOD app (ADR-0011).
"""Inspect Mach-O load commands and prove a relocatable app's native closure.

The component policy brackets this check with content manifests. This parser
reads bounded headers directly, rather than trusting a partial otool listing.
Apple's loader.h defines the wire format; dyld's run-path stack is described at
https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/RunpathDependentLibraries.html
https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h
"""

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import plistlib
import re
import stat
import struct
import sys
import time
# Only the exception type: plistlib rejects entity declarations itself.
from pyexpat import ExpatError


THIN = {b"\xce\xfa\xed\xfe": ("<", 28), b"\xcf\xfa\xed\xfe": ("<", 32),
        b"\xfe\xed\xfa\xce": (">", 28), b"\xfe\xed\xfa\xcf": (">", 32)}
FAT = {b"\xca\xfe\xba\xbe": (">", 20), b"\xbe\xba\xfe\xca": ("<", 20),
       b"\xca\xfe\xba\xbf": (">", 32), b"\xbf\xba\xfe\xca": ("<", 32)}
LOAD_DYLIB = {0xC, 0x80000018, 0x8000001F, 0x20, 0x80000023}
MAX_COMMAND_BYTES = 16 * 1024 * 1024
MAX_IMAGES = 4096
MAX_CONTEXTS = 100_000
SYSTEM_ROOTS = ("/usr/lib/", "/System/Library/Frameworks/",
                "/System/Library/PrivateFrameworks/")
# Apple silicon's first release; the floor when no slice requires more.
MACOS_FLOOR = (11, 0, 0)
PLIST_VERSION = re.compile(r"(0|[1-9][0-9]{0,3})(\.(0|[1-9][0-9]{0,2})){1,2}")
MAX_PLIST_BYTES = 1024 * 1024


class Invalid(ValueError):
    """An uninspectable or non-self-contained native package."""


def require(condition, message):
    if not condition:
        raise Invalid(message)


def reference(value):
    require(0 < len(value) <= 4096 and not any(ord(c) < 32 or ord(c) == 127 for c in value),
            "empty, oversized, or control-bearing native reference")
    return value


@dataclass(frozen=True)
class Image:
    cpu: int
    subtype: int
    kind: int
    imports: tuple
    rpaths: tuple
    macos: "tuple | None"  # minimum (major, minor, patch), or None when undeclared


def inspect(path):
    """Return all slices, or None for an ordinary non-native resource."""
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, "rb") as source:
        before = os.fstat(source.fileno())
        require(stat.S_ISREG(before.st_mode), "native inspection requires a regular file")

        def read(offset, size):
            require(0 <= offset <= before.st_size and 0 <= size <= before.st_size - offset,
                    "truncated Mach-O structure")
            source.seek(offset)
            value = source.read(size)
            require(len(value) == size, "incomplete Mach-O read")
            return value

        magic = source.read(4)
        if magic not in THIN and magic not in FAT:
            return None
        slices = [(0, before.st_size, None)]
        if magic in FAT:
            endian, entry_size = FAT[magic]
            count, = struct.unpack(endian + "I", read(4, 4))
            require(0 < count <= 16, "invalid architecture count")
            table = read(8, count * entry_size)
            slices = []
            for index in range(count):
                entry = table[index * entry_size:(index + 1) * entry_size]
                fields = struct.unpack(endian + ("IIIII" if entry_size == 20 else "IIQQII"), entry)
                cpu, subtype, offset, size, alignment = fields[:5]
                require(alignment <= 31 and offset % (1 << alignment) == 0
                        and offset >= 8 + len(table) and size >= 28
                        and offset <= before.st_size and size <= before.st_size - offset,
                        "invalid fat architecture extent")
                require(all(offset + size <= start or start + length <= offset
                            for start, length, _ in slices), "overlapping architecture slices")
                slices.append((offset, size, (cpu, subtype)))

        images = []
        for offset, size, expected in slices:
            magic = read(offset, 4)
            require(magic in THIN, "fat member is not a Mach-O image")
            endian, header_size = THIN[magic]
            header = struct.unpack(endian + "I" * (header_size // 4), read(offset, header_size))
            _, cpu, subtype, kind, count, command_bytes, *_ = header
            require(expected is None or expected == (cpu, subtype), "architecture header mismatch")
            require(kind in (2, 6, 8), "unsupported Mach-O file type")
            require(0 < count <= 65536 and count * 8 <= command_bytes <= MAX_COMMAND_BYTES
                    and command_bytes <= size - header_size, "invalid load-command extent")
            commands = read(offset + header_size, command_bytes)
            imports, rpaths, versions = [], [], []
            cursor = 0
            for _ in range(count):
                require(cursor + 8 <= len(commands), "missing load-command header")
                command, length = struct.unpack_from(endian + "II", commands, cursor)
                require(length >= 8 and length % (8 if header_size == 32 else 4) == 0
                        and length <= len(commands) - cursor,
                        "invalid load-command size")
                data = commands[cursor:cursor + length]
                # Reject legacy loaders and dyld environment overrides. They
                # introduce load behavior outside the dependency graph below.
                require(command not in (0x6, 0x7, 0x10, 0x27),
                        "unsupported native load behavior")
                if command in LOAD_DYLIB | {0xD, 0xE, 0x8000001C}:
                    minimum = 24 if command in LOAD_DYLIB | {0xD} else 12
                    require(length >= minimum, "truncated native path command")
                    start, = struct.unpack_from(endian + "I", data, 8)
                    require(minimum <= start < length, "invalid native string offset")
                    end = data.find(b"\0", start)
                    require(end >= start, "unterminated native path")
                    name = reference(data[start:end].decode("utf-8", errors="strict"))
                    if command in LOAD_DYLIB:
                        imports.append(name)
                    elif command == 0x8000001C:
                        rpaths.append(name)
                    elif command == 0xE:
                        require(name == "/usr/lib/dyld", "non-system dynamic linker")
                elif command == 0x24:  # LC_VERSION_MIN_MACOSX: version, sdk
                    require(length >= 16, "truncated macOS version command")
                    versions += struct.unpack_from(endian + "I", data, 8)
                elif command == 0x32:  # LC_BUILD_VERSION: platform, minos, sdk, ntools
                    require(length >= 24, "truncated build version command")
                    platform, minimum = struct.unpack_from(endian + "II", data, 8)
                    if platform == 1:  # PLATFORM_MACOS
                        versions.append(minimum)
                cursor += length
            require(cursor == command_bytes, "unaccounted load-command bytes")
            require(not any((image.cpu, image.subtype) == (cpu, subtype) for image in images),
                    "duplicate architecture")
            # Versions pack major.minor.patch into 16.8.8 bits; the largest is the newest.
            macos = max(versions, default=None)
            macos = None if macos is None else (macos >> 16, macos >> 8 & 0xFF, macos & 0xFF)
            images.append(Image(cpu, subtype, kind, tuple(imports), tuple(rpaths), macos))
            require(len(imports) + len(rpaths) <= 4096, "too many native references")
        after = os.fstat(source.fileno())
        require((before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                == (after.st_size, after.st_mtime_ns, after.st_ctime_ns),
                "native file changed during inspection")
        return tuple(images)


def inside(root, path):
    return path == root or root in path.parents


def system(name):
    # Apple libraries may exist only in the dyld shared cache. These reviewed
    # OS namespaces are the sole exception to file-existence/containment checks.
    return os.path.normpath(name) == name and any(name.startswith(root) for root in SYSTEM_ROOTS)


def compatible(image, cpu, subtype):
    base = {0x1000007: 3, 0x100000C: 0, 7: 3, 12: 0}.get(cpu)
    return image.cpu == cpu and (image.subtype == subtype or image.subtype == base)


def members(bundle):
    """Return a bundle's root, its native members' slices, and a shared time budget."""
    root = Path(bundle).resolve(strict=True)
    require(root.is_dir(), "bundle root is not a directory")
    deadline = time.monotonic() + 120
    native = {}
    entries = 0
    reference_bytes = 0

    def budget():
        require(time.monotonic() < deadline, "native closure exceeded its time budget")

    def walk_error(error):
        raise error

    for directory, dirs, files in os.walk(root, followlinks=False, onerror=walk_error):
        for name in dirs + files:
            budget()
            entries += 1
            require(entries <= 200_000, "bundle has too many members")
            path = Path(directory) / reference(name)
            require(inside(root, path.resolve(strict=True)), "bundle member escapes app")
            if path.is_symlink() or path.is_dir():
                continue
            images = inspect(path)
            require(images is not None or path.suffix not in (".dylib", ".so"),
                    "native library is not an inspectable Mach-O file")
            if images:
                reference_bytes += sum(len(value) for image in images
                                       for value in image.imports + image.rpaths)
                require(reference_bytes <= 16 * 1024 * 1024,
                        "bundle has too many native reference bytes")
                native[path] = images
                require(len(native) <= MAX_IMAGES, "bundle has too many native images")
    return root, native, budget


def validate(bundle):
    root, native, budget = members(bundle)
    executables = [(path, image) for path, images in native.items()
                   for image in images if image.kind == 2]
    require(executables, "bundle has no native executable")

    def expand(name, loader, executable):
        for token, base in (("@loader_path", loader.parent), ("@executable_path", executable.parent)):
            if name == token or name.startswith(token + "/"):
                return base / name[len(token):].lstrip("/")
        require(name.startswith("/"), "unsupported relative native path")
        return Path(name)

    def contained(path):
        target = path.resolve(strict=False)
        require(inside(root, target), "non-system native path escapes app")
        return target

    visited = set()

    def visit(path, image, executable, process_arch, inherited, active):
        budget()
        if path in active:
            return
        require(len(active) < 128, "native dependency graph is too deep")
        own = tuple(expand(name, path, executable) for name in image.rpaths)
        stack = tuple(dict.fromkeys(own + inherited))
        require(len(stack) <= 256, "native run-path stack is too large")
        key = (path, image, executable, process_arch, stack)
        if key in visited:
            return
        visited.add(key)
        require(len(visited) <= MAX_CONTEXTS, "too many native loader contexts")
        for name in image.imports:
            if system(name):
                continue
            require(not name.startswith("/"), "absolute non-system install name is not relocatable")
            if name.startswith("@rpath/"):
                require(stack, "@rpath dependency has no run-path context")
                # Do not let an external first candidate win on a build host.
                # Apple dependencies must use their explicit system install name.
                candidates = [contained(base / name[len("@rpath/"):]) for base in stack]
                target = next((candidate for candidate in candidates if candidate.is_file()), None)
                require(target is not None, "unresolved @rpath dependency")
            else:
                target = contained(expand(name, path, executable))
            require(target in native, "dependency does not name a bundled native file")
            matches = [candidate for candidate in native[target]
                       if compatible(candidate, *process_arch)]
            require(matches, "dependency has no compatible architecture")
            require(all(candidate.kind == 6 for candidate in matches),
                    "linked dependency is not a dylib")
            visit(target, matches[0], executable, process_arch, stack, active | {path})

    # Plugins can be dlopened rather than reached by a load command. Validate
    # every slice under each compatible main/helper executable context too.
    for path, images in native.items():
        for image in images:
            contexts = ([(path, image)] if image.kind == 2 else
                        [(exe, host) for exe, host in executables
                         if compatible(image, host.cpu, host.subtype)])
            require(contexts, "native slice has no compatible executable context")
            for exe, host in contexts:
                inherited = tuple(expand(name, exe, exe) for name in host.rpaths)
                visit(path, image, exe, (host.cpu, host.subtype), inherited, frozenset())
    return len(native)


def version_text(version):
    major, minor, patch = version
    return f"{major}.{minor}" + (f".{patch}" if patch else "")


def required_macos(native):
    """Return the newest macOS any native slice requires, never below the floor."""
    versions = [image.macos for images in native.values() for image in images]
    require(None not in versions, "native slice declares no minimum macOS")
    return max(versions + [MACOS_FLOOR])


def declared_macos(root):
    """Return Info.plist's LSMinimumSystemVersion, failing closed if absent or malformed."""
    descriptor = os.open(root / "Contents/Info.plist", os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, "rb") as source:
        require(stat.S_ISREG(os.fstat(source.fileno()).st_mode), "Info.plist is not a regular file")
        data = source.read(MAX_PLIST_BYTES + 1)
    require(len(data) <= MAX_PLIST_BYTES, "Info.plist is oversized")
    try:
        properties = plistlib.loads(data)
    except (ValueError, ExpatError) as error:
        raise Invalid("Info.plist is not a parseable property list") from error
    value = properties.get("LSMinimumSystemVersion") if isinstance(properties, dict) else None
    if not (isinstance(value, str) and PLIST_VERSION.fullmatch(value)):
        raise Invalid("Info.plist has no parseable LSMinimumSystemVersion")
    parts = tuple(int(part) for part in value.split("."))
    return parts + (0,) * (3 - len(parts))


def check_minimum(bundle):
    """Require Info.plist to declare at least the newest macOS a native slice requires."""
    root, native, _ = members(bundle)
    required, declared = required_macos(native), declared_macos(root)
    require(declared >= required,
            f"a bundled Mach-O requires macOS {version_text(required)}, "
            f"above LSMinimumSystemVersion {version_text(declared)}")
    return required


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("bundle", "minimum", "imports", "rpaths", "kind"))
    parser.add_argument("path", type=Path)
    parser.add_argument("--report-rejection", action="store_true",
                        help="also emit a bounded, path-free rejection on stdout for package gates")
    arguments = parser.parse_args()
    try:
        if arguments.mode == "bundle":
            count = validate(arguments.path)
            required = check_minimum(arguments.path)
            print(f"Native closure verified for {count} Mach-O files (all architectures); "
                  f"Info.plist covers their minimum macOS {version_text(required)}.")
        elif arguments.mode == "minimum":
            print(version_text(required_macos(members(arguments.path)[1])))
        else:
            images = inspect(arguments.path)
            if not images:
                raise Invalid("expected a native file")
            if arguments.mode == "kind":
                kinds = {image.kind for image in images}
                require(len(kinds) == 1, "native slices disagree on file type")
                print(kinds.pop())
                return 0
            values = (value for image in images for value in getattr(image, arguments.mode))
            for value in dict.fromkeys(values):
                print(value)
    except (Invalid, OSError, UnicodeError, struct.error, RuntimeError) as error:
        print(f"macOS native closure rejected: {error}", file=sys.stderr)
        if arguments.report_rejection:
            reason = str(error) if isinstance(error, Invalid) else type(error).__name__
            print(f"macOS native closure rejected: {reason[:256]}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
