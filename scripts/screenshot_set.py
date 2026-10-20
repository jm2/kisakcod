#!/usr/bin/env python3
r"""
Screenshot regression set for the native clients (NOW row 50, CLIENT.md "Screenshots").

  capture  runs the client on each map (+devmap, cheats on), joins a team, then for each
           fixed view moves the eye with setviewpos (noclip, god, gun and HUD hidden), settles
           and writes `screenshot` TGAs, twice per view (A and A2) so compare can measure the
           noise floor. Capture master first, then the PR with master's anchors, back to back
           on the same host:
             screenshot_set.py capture --client <exe> --data <copy> --out ../shots/main
             screenshot_set.py capture --client <exe> --data <copy> --out ../shots/pr \\
                 --anchors ../shots/main/screenshot_set.json
  compare  per view: mean and max absolute difference, the share of pixels whose largest
           channel is off by more than 16/255, the same share counting only pixels where the
           head differs from both base shots by more than they differ from each other
           (A/B/A), and black, flat and no-sky warnings on the head. It also writes a local
           contact sheet (base, head, difference x4). It sets no thresholds.

--data must be a copy of the retail install (a profile is seeded in it if none exists), and
--out must be outside the checkout. Retail images never leave the host: post only numbers.
The client is driven over rcon (the Windows client has no stdin console); progress is read
from its console_mp.log. Standard library only.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import secrets
import shutil
import socket
import struct
import subprocess  # nosec B404 - runs only the client binary named on the command line, with a fixed argv
import sys
import tempfile
import time
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
MAPS = ["mp_crash", "mp_shipment"]
# Eye positions (x, y, z, yaw) the views are offsets from. Spawns are random; a map missing
# here uses the first capture's spawn, recorded in screenshot_set.json for --anchors.
ANCHORS: dict[str, tuple[float, float, float, float]] = {}
# name, (forward, right, up) offset, yaw delta, pitch (negative looks up)
VIEWS = [
    ("forward", (0, 0, 0), 0, 0),
    ("ground", (0, 0, 0), 0, 30),
    ("air", (0, 0, 600), 0, 35),
    ("air_back", (0, 0, 600), 180, 35),
    ("sky", (0, 0, 0), 90, -45),
]
PINNED = [("r_fullscreen", "0"), ("r_mode", "800x600"), ("r_aaSamples", "1"), ("r_vsync", "0"),
          ("com_maxfps", "60"), ("com_introPlayed", "1"), ("db_unverified64", "1"), ("logfile", "2")]
ACTIVE = re.compile(r"byte snapshot for ")
VIEWPOS = re.compile(r"^\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+) (-?\d+)")


class Client:
    """The client as a listen server, driven over rcon, its console read from its log."""

    def __init__(self, exe: Path, data: Path, home: Path, map_name: str, extra: list[str], port: int):
        """Start the client on map_name with the pinned dvars and a fresh rcon password."""
        self.password = secrets.token_hex(8)
        self.port = port
        self.log_path = home / "main" / "console_mp.log"
        self.offset = 0
        self.last_send = 0.0
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        args = [str(exe), "+set", "fs_basepath", str(data), "+set", "fs_homepath", str(home),
                "+set", "net_port", str(port), "+set", "rcon_password", self.password]
        for name, value in PINNED:
            args += ["+set", name, value]
        for i in range(0, len(extra), 2):
            args += ["+set", extra[i], extra[i + 1]]
        args += ["+devmap", map_name]
        self.proc = subprocess.Popen(args, cwd=str(data), stdin=subprocess.DEVNULL,  # nosec B603  # nosemgrep
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def send(self, command: str):
        # the server drops an rcon packet within 500 ms of the previous one
        time.sleep(max(0.0, self.last_send + 0.6 - time.monotonic()))
        self.last_send = time.monotonic()
        packet = b"\xff\xff\xff\xffrcon " + f"{self.password} {command}".encode("ascii")
        self.sock.sendto(packet, ("127.0.0.1", self.port))

    def ask(self, command: str, pattern: re.Pattern, timeout: float = 5.0):
        """Output printed while the command runs goes into the rcon reply, not the log."""
        self.sock.settimeout(0.05)
        try:
            while True:
                self.sock.recvfrom(65536)  # discard earlier replies
        except OSError:
            pass
        self.sock.settimeout(timeout)
        self.send(command)
        try:
            while True:
                reply = self.sock.recvfrom(65536)[0][4:].decode("ascii", "replace")
                for line in reply.splitlines():
                    if m := pattern.search(line):
                        return m
        except OSError:
            return None

    def wait_for(self, pattern: re.Pattern, timeout: float):
        """The first new log line matching pattern, or None at timeout or exit."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.log_path.exists():
                with self.log_path.open("rb") as f:
                    f.seek(self.offset)
                    chunk = f.read()
                end = chunk.rfind(b"\n") + 1
                self.offset += end
                for line in chunk[:end].decode("utf-8", "replace").splitlines():
                    if m := pattern.search(line):
                        return m
            if self.proc.poll() is not None:
                return None
            time.sleep(0.2)
        return None

    def close(self):
        self.send("quit")
        try:
            self.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=30)
        self.sock.close()


def view_target(anchor, offset, yaw_delta, pitch):
    x, y, z, yaw = anchor
    rad = math.radians(yaw)
    fwd, right, up = offset
    return (x + fwd * math.cos(rad) + right * math.sin(rad),
            y + fwd * math.sin(rad) - right * math.cos(rad), z + up, (yaw + yaw_delta) % 360.0, pitch)


def wait_for_file(home: Path, name: str, timeout: float) -> Path | None:
    """The screenshot once it exists and its size has stopped changing."""
    deadline, last = time.monotonic() + timeout, -1
    while time.monotonic() < deadline:
        path = next(iter(home.glob(f"**/screenshots/{name}")), None)
        size = path.stat().st_size if path else -1
        if size > 0 and size == last:
            return path
        last = size
        time.sleep(0.5)
    return None


def seed_profile(data: Path):
    profiles = data / "players" / "profiles"
    if not (profiles / "active.txt").exists():
        (profiles / "shots").mkdir(parents=True, exist_ok=True)
        (profiles / "shots" / "config_mp.cfg").write_text('seta name "shots"\n', encoding="ascii")
        (profiles / "active.txt").write_text("shots", encoding="ascii")


def capture_view(client: Client, home: Path, out: Path, map_name: str, anchor, view, settle: float) -> dict:
    """Move the eye to one view, check it landed, and take its A and A2 shots."""
    name, offset, yaw_delta, pitch = view
    x, y, z, yaw, pitch = view_target(anchor, offset, yaw_delta, pitch)
    client.send(f"setviewpos {x:.1f} {y:.1f} {z:.1f} {yaw:.1f} {pitch:.1f}")
    time.sleep(settle)
    m = client.ask("viewpos", VIEWPOS)
    placed = bool(m) and abs(float(m.group(1)) - x) < 2 and abs(float(m.group(2)) - y) < 2
    files = []
    for rep in ("a", "a2"):
        shot = f"{map_name}_{name}_{rep}"
        err = client.ask(f"screenshot {shot}", re.compile(r"ERROR.*"), 1.0)
        if err:
            print(f"  {shot}: {err.group(0)}", flush=True)
        src = wait_for_file(home, f"{shot}.tga", 30)
        if src:
            shutil.move(str(src), out / f"{shot}.tga")
        files.append(f"{shot}.tga" if src else None)
        time.sleep(settle)
    print(f"  {map_name} {name}: {sum(1 for f in files if f)}/2 written"
          f"{'' if placed else ', VIEW NOT PLACED'}", flush=True)
    return {"map": map_name, "view": name, "files": files, "placed": placed,
            "at": [round(x, 1), round(y, 1), round(z, 1), round(yaw, 1), pitch]}


def capture_map(a, out: Path, map_name: str, anchors: dict) -> list[dict]:
    home = Path(tempfile.mkdtemp(prefix="kisak-shots-"))
    client = Client(Path(a.client), Path(a.data), home, map_name, a.set or [], a.port)
    shots = []
    try:
        if not client.wait_for(ACTIVE, 300):
            raise RuntimeError(f"{map_name}: the client never became active")
        time.sleep(a.settle)
        client.send("openScriptMenu team_marinesopfor autoassign")
        time.sleep(2)
        client.send("openScriptMenu changeclass assault_mp,0")
        time.sleep(3 * a.settle)
        extra = a.set or []
        # cheat dvars, including --set ones, take effect only once the devmap is up
        for command in ["god", "noclip", "cg_drawGun 0", "cg_draw2D 0"] + [
                f"{extra[i]} {extra[i + 1]}" for i in range(0, len(extra), 2)]:
            client.send(command)
        time.sleep(3 * a.settle)  # past the spawn's fade-in, which the first view would catch
        if map_name not in anchors:
            m = client.ask("viewpos", VIEWPOS)
            if not m:
                raise RuntimeError(f"{map_name}: no viewpos answer")
            anchors[map_name] = tuple(float(m.group(i)) for i in range(1, 5))
        for view in VIEWS:
            shots.append(capture_view(client, home, out, map_name, anchors[map_name], view, a.settle))
    finally:
        client.close()
        shutil.rmtree(home, ignore_errors=True)
    return shots


def cmd_capture(a) -> int:
    out = Path(a.out).resolve()
    if out == REPO_ROOT or REPO_ROOT in out.parents:
        raise SystemExit("--out must be outside the checkout: retail images never enter the repo")
    out.mkdir(parents=True, exist_ok=True)
    seed_profile(Path(a.data))
    anchors = {k: tuple(v) for k, v in ANCHORS.items()}
    if a.anchors:
        anchors.update({k: tuple(v) for k, v in json.loads(Path(a.anchors).read_text())["anchors"].items()})
    meta = {"client": a.client, "label": a.label, "set": a.set or [], "shots": [], "failed": []}
    for map_name in (a.maps.split(",") if a.maps else MAPS):
        print(map_name, flush=True)
        try:
            meta["shots"] += capture_map(a, out, map_name, anchors)
        except RuntimeError as e:
            print(f"  FAILED: {e}", flush=True)
            meta["failed"].append({"map": map_name, "reason": str(e)})
    meta["anchors"] = anchors
    (out / "screenshot_set.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    missing = sum(f is None for s in meta["shots"] for f in s["files"])
    print(f"{2 * len(meta['shots']) - missing} shots written, {missing} missing, {len(meta['failed'])} map(s) failed")
    return 1 if missing or meta["failed"] else 0


# --- images: uncompressed or RLE 24/32-bit TGA in, PNG out ---

def read_tga(path: Path) -> tuple[int, int, bytes]:
    """Width, height and top-down BGR rows."""
    raw = path.read_bytes()
    id_len, cmap, kind = raw[0], raw[1], raw[2]
    width, height, bpp, desc = struct.unpack_from("<HHBB", raw, 12)
    if cmap or kind not in (2, 10) or bpp not in (24, 32):
        raise ValueError(f"{path.name}: unsupported TGA (type {kind}, {bpp} bpp)")
    px, pos, n = bpp // 8, 18 + id_len, width * height
    if kind == 2:
        data = raw[pos:pos + n * px]
    else:
        buf = bytearray()
        while len(buf) < n * px:
            head = raw[pos]
            pos += 1
            count = (head & 0x7F) + 1
            if head & 0x80:
                buf += raw[pos:pos + px] * count
                pos += px
            else:
                buf += raw[pos:pos + count * px]
                pos += count * px
        data = bytes(buf[:n * px])
    if px == 4:
        data = b"".join(data[i:i + 3] for i in range(0, len(data), 4))
    if not desc & 0x20:
        stride = width * 3
        data = b"".join(data[r * stride:(r + 1) * stride] for r in range(height - 1, -1, -1))
    return width, height, data


def write_png(path: Path, width: int, height: int, rgb: bytes):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))
    rows = b"".join(b"\0" + rgb[r * width * 3:(r + 1) * width * 3] for r in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def pixel_diff(a: bytes, b: bytes) -> list[int]:
    """The largest channel difference of each pixel."""
    return [max(abs(x0 - y0), abs(x1 - y1), abs(x2 - y2))
            for x0, x1, x2, y0, y1, y2 in zip(a[0::3], a[1::3], a[2::3], b[0::3], b[1::3], b[2::3])]


def stats(data: bytes) -> tuple[float, float]:
    n = len(data) or 1
    mean = sum(data) / n
    return mean, math.sqrt(max(0.0, sum(v * v for v in data) / n - mean * mean))


def warnings(view: str, width: int, height: int, data: bytes) -> list[str]:
    out = []
    mean, std = stats(data[::7])
    if mean < 8:
        out.append("black")
    if std < 2:
        out.append("flat")
    if view == "sky" and stats(data[int(height * 0.05) * width * 3:int(height * 0.4) * width * 3:7])[1] < 1.5:
        out.append("no sky")
    return out


def thumb(width: int, height: int, data: bytes, step: int) -> list[bytes]:
    """Rows of a 1/step thumbnail as RGB."""
    rows = []
    for r in range(0, height - step + 1, step):
        row = data[r * width * 3:(r + 1) * width * 3]
        px = b"".join(row[c * 3:c * 3 + 3] for c in range(0, width - step + 1, step))
        rows.append(bytes(px[i + j] for i in range(0, len(px), 3) for j in (2, 1, 0)))
    return rows


def compare_view(key: tuple[str, str], bf: list[Path], hf: list[Path]) -> tuple[str, list[bytes]]:
    """One view's report row, and its contact sheet rows (none when it can't be compared)."""
    if not bf or not hf:
        return f"| {key[0]} | {key[1]} | missing in {'base' if not bf else 'head'} | | | | | |", []
    w, h, img_a = read_tga(bf[0])
    hw, hh, img_h = read_tga(hf[0])
    if (w, h) != (hw, hh):
        return f"| {key[0]} | {key[1]} | size {w}x{h} vs {hw}x{hh} | | | | | |", []
    d = pixel_diff(img_a, img_h)
    n = len(d)
    noise = excess = "n/a"
    if len(bf) > 1:
        img_a2 = read_tga(bf[1])[2]
        d_aa, d_a2h = pixel_diff(img_a, img_a2), pixel_diff(img_a2, img_h)
        noise = f"{100 * sum(v > 16 for v in d_aa) / n:.2f}%"
        excess = f"{100 * sum(x > 16 and x > z and y > z for x, y, z in zip(d, d_a2h, d_aa)) / n:.2f}%"
    warn = ", ".join(warnings(key[1], w, h, img_h)) or "-"
    row = (f"| {key[0]} | {key[1]} | {sum(d) / n:.2f} | {max(d)} | {100 * sum(v > 16 for v in d) / n:.2f}% | "
           f"{excess} | {noise} | {warn} |")
    diff_img = bytes(min(255, 4 * v) for v in d for _ in range(3))
    parts = [thumb(w, h, img_a, 4), thumb(w, h, img_h, 4), thumb(w, h, diff_img, 4)]
    rows = [b"".join(p[r] for p in parts) for r in range(len(parts[0]))]
    return row, rows + [b"\x40" * len(rows[0])] * 4


def cmd_compare(a) -> int:
    base, head, out = Path(a.base), Path(a.head), Path(a.out).resolve()
    if out == REPO_ROOT or REPO_ROOT in out.parents:
        raise SystemExit("--out must be outside the checkout")
    out.mkdir(parents=True, exist_ok=True)

    def load(d: Path) -> dict:
        return {(s["map"], s["view"]): s["files"] for s in json.loads((d / "screenshot_set.json").read_text())["shots"]}
    bs, hs = load(base), load(head)
    report = ["| map | view | mean | max | >16 | >16 beyond A/A2 noise | noise (A vs A2) | warnings on head |",
              "|---|---|---|---|---|---|---|---|"]
    sheet_rows, problems = [], 0
    for key in sorted(set(bs) | set(hs), key=lambda k: (k[0], [v[0] for v in VIEWS].index(k[1]))):
        row, rows = compare_view(key, [base / f for f in bs.get(key, []) if f], [head / f for f in hs.get(key, []) if f])
        report.append(row)
        problems += not rows
        sheet_rows += rows
    text = "\n".join(report)
    (out / "compare.md").write_text(text + "\n", encoding="utf-8")
    print(text)
    if sheet_rows:
        width = len(sheet_rows[0]) // 3
        write_png(out / "compare_sheet.png", width, len(sheet_rows), b"".join(sheet_rows))
        print(f"contact sheet (base | head | diff x4, local only): {out / 'compare_sheet.png'}")
    return 1 if problems else 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture", help="capture the set from one client build")
    c.add_argument("--client", required=True, help="KisakCOD-mp binary under test")
    c.add_argument("--data", required=True, help="a copy of the retail install (fs_basepath)")
    c.add_argument("--out", required=True, help="output folder outside the checkout")
    c.add_argument("--maps", help="comma-separated, default " + ",".join(MAPS))
    c.add_argument("--anchors", help="an earlier capture's screenshot_set.json: reuse its eye positions")
    c.add_argument("--label", default="", help="recorded in screenshot_set.json, e.g. the commit")
    c.add_argument("--settle", type=float, default=2.0, help="seconds after each move and shot")
    c.add_argument("--port", type=int, default=28960)
    c.add_argument("--set", nargs=2, action="extend", metavar=("DVAR", "VALUE"), help="extra dvars, repeatable")
    k = sub.add_parser("compare", help="compare two captures from the same host")
    k.add_argument("base")
    k.add_argument("head")
    k.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    return cmd_capture(a) if a.cmd == "capture" else cmd_compare(a)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
