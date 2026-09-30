#!/usr/bin/env python3
"""Native 64-bit census: KPIs K1, K2, K3 and K5 (docs/design/NATIVE64.md).

For each target (win32 control, win64, winarm64, lin64, a64, and mac64 on a
macOS host) every headless dedicated server TU is compiled with clang
-fsyntax-only. Failures are split into "assert-only" (only static_assert
size/offset failures) and "other". Win64 then gets a real link attempt and
a clearly labelled probe link that neutralises static asserts; the probe
never gates anything. K3 counts the upstream engine TUs the Linux test build
compiles.

Writes <out>/census.json and <out>/census.md. Never fails on a low KPI; it
exits non-zero only when the census itself cannot run.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STUBS = ROOT / "scripts/ci/ci-stubs"
GENERATED = ROOT / "build-census/generated"  # generated headers; main() puts them under --out
DEFS = ["-DKISAK_MP", "-DKISAK_DEDICATED", "-DDEDICATED", "-DKISAK_DEDI_HEADLESS", "-DNDEBUG"]
WIN_DEFS = ["-DWIN32", "-D_WINDOWS", "-D_CONSOLE", "-D_MBCS"]
TARGETS = {
    "win32": ("i686-w64-mingw32", WIN_DEFS, "windows"),
    "win64": ("x86_64-w64-mingw32", WIN_DEFS, "windows"),
    # Windows ARM64 checks the win64 set; main() adds the llvm-mingw sysroot. The force-include works
    # around a clang 18 builtin bug (ci-stubs/winarm64/prefetch.h).
    "winarm64": ("aarch64-w64-mingw32",
                 WIN_DEFS + ["-stdlib=libc++", "-include", str(STUBS / "winarm64/prefetch.h")], "windows"),
    "lin64": ("x86_64-linux-gnu", ["-DUNIX"], "d3d"),
    "a64": ("aarch64-linux-gnu", ["-DUNIX"], "d3d"),
    # Apple clang and the macOS SDK, so only on a macOS host (the "Native64 census / mac64" CI job).
    "mac64": ("arm64-apple-macos", ["-DUNIX"], "d3d"),
}
# winarm64's Windows ARM64 headers and libc++: this llvm-mingw release's sysroot. Its LLVM 18.1.8 matches
# the runner's clang 18, so winarm64 and win64 differ by target and headers, not by compiler.
LLVM_MINGW = ("https://github.com/mstorsjo/llvm-mingw/releases/download/20240619/"
              "llvm-mingw-20240619-ucrt-ubuntu-20.04-x86_64.tar.xz",
              "27d33157cc252c29ad6f777a96a0d94176fea1b534ff09b5071485def143b90e")
WARN = ["-Wno-everything", "-Werror=c++11-narrowing", "-Wvoid-pointer-to-int-cast",
        "-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wshorten-64-to-32",
        "-W#pragma-messages"]
WIN_LIBS = ["-lws2_32", "-lwinmm", "-luser32", "-lgdi32", "-ladvapi32", "-lshell32",
            "-lole32", "-loleaut32", "-luuid", "-ldbghelp", "-lpsapi", "-lshlwapi"]
DIAG = re.compile(r"^(?P<file>[^\s:][^:]*):(?P<line>\d+):\d+: (?:fatal )?error: (?P<msg>.*)$")
UNDEF = re.compile(r"undefined symbol: (.+)$")
D3D_MARK = "kisak-census-d3d9-stub"
# Not engine code: the level editor and the vendored ODE and Speex sources.
K3_EXCLUDE = ("src/radiant/", "src/physics/ode/", "src/groupvoice/speex/")


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def tu_lists() -> dict[str, list[str]]:
    res = run(["cmake", "-DROOT=%s" % ROOT, "-P", "scripts/ci/native64-tulist.cmake"])
    if res.returncode:
        sys.exit("tulist failed:\n" + res.stderr)
    win = [os.path.relpath(p, ROOT) for p in res.stderr.split() if p.endswith((".c", ".cpp"))]
    posix = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "src/_platform/posix").glob("*.c*"))
    lin = [t for t in win if not t.startswith(("src/win32/", "src/_platform/win32/"))] + posix
    mac = lin + sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "src/_platform/macos").glob("*.c*"))
    return {"win32": win, "win64": win, "winarm64": win, "lin64": lin, "a64": lin, "mac64": mac}


def fetch_tracy(out: Path) -> Path:
    """Fetch exactly the Tracy commit tracy.cmake pins."""
    pin = re.search(r"GIT_TAG\s+([0-9a-f]{40})\b", (ROOT / "scripts/extern/tracy.cmake").read_text())
    if not pin:
        sys.exit("scripts/extern/tracy.cmake must pin GIT_TAG to a full commit SHA")
    dest = out / ("tracy-" + pin.group(1))
    if not (dest / "public").is_dir():
        for cmd in (["git", "init", "-q", str(dest)],
                    ["git", "-C", str(dest), "fetch", "-q", "--depth", "1",
                     "https://github.com/wolfpld/tracy.git", pin.group(1)],
                    ["git", "-C", str(dest), "checkout", "-q", "FETCH_HEAD"]):
            run(cmd, check=True)
    return dest / "public"


def fetch_llvm_mingw(out: Path) -> Path:
    """Fetch the pinned llvm-mingw release, check its digest, and unpack only the ARM64 sysroot headers."""
    url, digest = LLVM_MINGW
    dest = out / Path(url).name.removesuffix(".tar.xz")
    if not (dest / "aarch64-w64-mingw32/include/windows.h").is_file():
        with urllib.request.urlopen(url, timeout=300) as res:
            data = res.read()
        if hashlib.sha256(data).hexdigest() != digest:
            sys.exit("llvm-mingw download does not match its pinned sha256: " + url)
        with tarfile.open(fileobj=io.BytesIO(data), mode="r|xz") as tar:
            for member in tar:
                if member.name.split("/", 1)[-1].startswith(("generic-w64-mingw32/include",
                                                              "aarch64-w64-mingw32/include")):
                    tar.extract(member, out, filter="data")
    return dest


def compile_cmd(cfg: str, tu: str, tracy: Path, extra: list[str]) -> list[str]:
    triple, defs, stub = TARGETS[cfg]
    lang = ["clang", "-x", "c", "-std=gnu11"] if tu.endswith(".c") else ["clang++", "-x", "c++", "-std=c++20"]
    return lang + ["--target=" + triple, "-fms-extensions", "-fdelayed-template-parsing", *DEFS, *defs,
                   "-I", "src", "-I", "deps", "-I", str(tracy), "-I", str(GENERATED), "-I", str(STUBS / "common"),
                   "-I", str(STUBS / stub), *WARN, "-ferror-limit=0", "-fno-color-diagnostics",
                   "-fno-caret-diagnostics", *extra, tu]


def classify(stderr: str, rc: int) -> dict:
    errors = [dict(m.groupdict(), file=os.path.normpath(m["file"])) for m in map(DIAG.match, stderr.splitlines()) if m]
    asserts = [e for e in errors if e["msg"].startswith(("static assertion failed", "static_assert failed"))
               and re.search(r"sizeof|offsetof|alignof", e["msg"])]
    other = [e for e in errors if e not in asserts]
    kind = "pass" if rc == 0 else ("assert-only" if asserts and not other else "other")
    if rc and not errors:
        other = [{"file": "?", "line": "0", "msg": stderr.strip().splitlines()[-1] if stderr.strip() else "rc=%d" % rc}]
    return {"kind": kind, "asserts": asserts, "other": other, "d3d": D3D_MARK in stderr}


def census(cfg: str, tus: list[str], tracy: Path, jobs: int) -> dict:
    def one(tu):
        res = run(compile_cmd(cfg, tu, tracy, ["-fsyntax-only"]))
        return tu, classify(res.stderr, res.returncode)
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        results = dict(ex.map(one, tus))
    kinds = collections.Counter(r["kind"] for r in results.values())
    first_other = collections.Counter(r["other"][0]["file"] for r in results.values() if r["other"])
    assert_sites = {(e["file"], e["line"]) for r in results.values() for e in r["asserts"]}
    by_header = collections.Counter(Path(f).name for f, _ in assert_sites)
    return {
        "total": len(tus), "pass": kinds["pass"], "assert_only": kinds["assert-only"], "other": kinds["other"],
        "d3d_stub_tus": sum(r["d3d"] for r in results.values()),
        "top_first_error_files": first_other.most_common(10),
        "size_assert_sites": len(assert_sites), "size_asserts_by_header": by_header.most_common(),
        "failing": {tu: r["kind"] for tu, r in sorted(results.items()) if r["kind"] != "pass"},
    }


def win64_link(tus: list[str], tracy: Path, out: Path, jobs: int, probe: bool) -> dict:
    objdir = out / ("obj-probe" if probe else "obj-real")
    shutil.rmtree(objdir, ignore_errors=True)
    objdir.mkdir(parents=True)
    extra = ["-include", str(STUBS / "probe/neutralize_asserts.h")] if probe else []

    def one(i_tu):
        i, tu = i_tu
        obj = objdir / ("%04d.o" % i)
        res = run(compile_cmd("win64", tu, tracy, ["-c", "-O0", "-o", str(obj), *extra]))
        return tu, obj if res.returncode == 0 else None
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        built = list(ex.map(one, enumerate(tus)))
    objs = [str(o) for _, o in built if o]
    excluded = [tu for tu, o in built if not o]
    exe = out / ("KisakCOD-dedi-win64-probe.exe" if probe else "KisakCOD-dedi-win64.exe")
    res = run(["clang++", "--target=x86_64-w64-mingw32", "-fuse-ld=lld", "-o", str(exe),
               *objs, *WIN_LIBS, "-Wl,--error-limit=0"])
    undefined = sorted({m.group(1) for m in map(UNDEF.search, res.stderr.splitlines()) if m})
    # A real link only counts when every TU built; the probe reports what links from the objects that did.
    linked = res.returncode == 0 and (probe or not excluded)
    return {"status": "linked" if linked else "failed", "excluded_tus": len(excluded),
            "excluded": excluded, "undefined": len(undefined), "undefined_sample": undefined[:20]}


def k3(out: Path) -> dict:
    sha = re.search(r"Last synced \| `([0-9a-f]{7,40})`", (ROOT / "docs/UPSTREAM.md").read_text()).group(1)
    upstream = run(["git", "ls-tree", "-r", "--name-only", sha, "--", "src"], check=True).stdout.split()
    engine = {f for f in upstream if f.endswith((".c", ".cpp")) and not f.startswith(K3_EXCLUDE)}
    build = out / "k3-build"
    # Clang, like the CI leg that runs the engine-TU tests (Portable tests / ASan + UBSan): those targets are
    # clang-only by the toolchain policy (PLATFORM_POSIX.md), so a GCC configure would not count them.
    run(["cmake", "-S", ".", "-B", str(build), "-DKISAK_BUILD_MP=OFF", "-DKISAK_BUILD_DEDICATED=OFF",
         "-DKISAK_BUILD_SP=OFF", "-DBUILD_TESTING=ON", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
         "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"], check=True)
    compiled = {os.path.relpath(os.path.realpath(e["file"]), ROOT)
                for e in json.loads((build / "compile_commands.json").read_text())}
    # Tests that #include an engine .c/.cpp compile it too.
    inc = run(["git", "grep", "-h", "-E", r'#include\s*[<"][^">]*\.(c|cpp)[">]', "--", "tests"]).stdout
    compiled |= {"src/" + m for m in re.findall(r'[<"]([^">]+\.(?:c|cpp))[">]', inc)}
    hit = sorted(engine & compiled)
    return {"upstream_sha": sha, "compiled": len(hit), "total": len(engine), "files": hit}


def markdown(c: dict) -> str:
    out = ["## Native64 census", "", "| Target | Pass | Assert-only | Other | Reaches d3d9.h stub |",
           "|---|---:|---:|---:|---:|"]
    for cfg, t in c["targets"].items():
        out.append("| %s | %d/%d | %d | %d | %s |" % (cfg, t["pass"], t["total"], t["assert_only"], t["other"],
                                                      t["d3d_stub_tus"] if TARGETS[cfg][2] == "d3d" else "-"))
    for cfg in ("win64", "winarm64", "lin64", "mac64"):
        t = c["targets"].get(cfg)
        if t:
            out += ["", "**%s: top files with the first non-assert error**" % cfg, ""]
            out += ["- `%s`: %d TUs" % kv for kv in t["top_first_error_files"]] or ["- none"]
    t = c["targets"].get("win64")
    if t:
        out += ["", "**Failing 64-bit size/offset asserts (win64): %d sites**" % t["size_assert_sites"], ""]
        out += ["- `%s`: %d" % kv for kv in t["size_asserts_by_header"]]
    for kind in ("real", "probe"):
        link = c.get("link", {}).get("win64", {}).get(kind)
        if link:
            label = "Win64 real link" if kind == "real" else "Win64 probe link (asserts neutralised; never gates)"
            out.append("")
            out.append("**%s:** %s; %s TUs excluded; %s undefined symbols" % (
                label, link["status"], link.get("excluded_tus", 0), link.get("undefined", "-")))
            if kind == "probe" and link.get("excluded"):
                out.append("Excluded (still fail with asserts neutralised): " + ", ".join(
                    "`%s`" % t for t in link["excluded"]))
    if "k3" in c:
        out += ["", "**K3:** %d/%d upstream engine TUs compiled by the Linux test build" % (
            c["k3"]["compiled"], c["k3"]["total"])]
    out += ["", "Census time: %.0f s" % c["seconds"]]
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=ROOT / "build-census")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--targets", default="win32,win64,winarm64,lin64,a64")
    ap.add_argument("--no-link", action="store_true")
    ap.add_argument("--no-k3", action="store_true")
    args = ap.parse_args()
    targets = args.targets.split(",")
    if "mac64" in targets and sys.platform != "darwin":
        sys.exit("mac64 needs Apple clang and the macOS SDK: run it on a macOS host")
    args.out.mkdir(parents=True, exist_ok=True)
    start = time.time()
    tracy = fetch_tracy(args.out)
    # The disk32 mirrors 64-bit TUs include, generated as the CMake build does.
    global GENERATED
    GENERATED = args.out.resolve() / "generated"
    run([sys.executable, "scripts/gen_disk32.py", "src/database/db_disk32.schema",
         str(GENERATED / "database/db_disk32_mirrors.h")], check=True)
    lists = tu_lists()
    result = {"schema": 1, "commit": os.environ.get("GITHUB_SHA") or run(["git", "rev-parse", "HEAD"]).stdout.strip(),
              "targets": {}}
    if "winarm64" in targets:
        triple, defs, stub = TARGETS["winarm64"]
        TARGETS["winarm64"] = (triple, defs + ["--sysroot=%s" % fetch_llvm_mingw(args.out)], stub)
    for cfg in targets:
        result["targets"][cfg] = census(cfg, lists[cfg], tracy, args.jobs)
        t = result["targets"][cfg]
        print("%s: %d/%d pass, %d assert-only, %d other" % (cfg, t["pass"], t["total"], t["assert_only"], t["other"]))
    if "lin64" in result["targets"]:
        result["d3d_stub_tus"] = result["targets"]["lin64"]["d3d_stub_tus"]
    if not args.no_link and "win64" in result["targets"]:
        # The census attempts the real link even while TUs fail to compile, so
        # K2 reports the attempt (excluded TUs, undefined symbols) instead of
        # "not attempted". A "linked" verdict still requires every TU to build
        # (win64_link); the probe reports what links from the objects that did.
        real = win64_link(lists["win64"], tracy, args.out, args.jobs, probe=False)
        probe = win64_link(lists["win64"], tracy, args.out, args.jobs, probe=True)
        result["link"] = {"win64": {"real": real, "probe": probe}}
    if not args.no_k3:
        result["k3"] = k3(args.out)
    result["seconds"] = round(time.time() - start)
    (args.out / "census.json").write_text(json.dumps(result, indent=1) + "\n")
    (args.out / "census.md").write_text(markdown(result))
    print(markdown(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
