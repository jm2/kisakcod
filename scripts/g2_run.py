#!/usr/bin/env python3
"""The G2 run (docs/NOW.md row 45): the 64-bit headless server, built with
ASan and UBSan, boots mp_shipment (db_unverified64), answers getinfo and
getstatus, rotates the five G2 maps and runs for the given time.

This is a manual run on an agent host's own Steam 1.8 install. No retail data
is in the repository or in CI; the install path is an argument, and the
evidence the run leaves is text: the server's console log and the query
replies, in the work directory.

The server's configs set their own rotation after the command line, so the
G2 rotation is set from the server console once the first map is up. The
first map is the rotation's first, so four map_rotate commands walk the rest.
After the rotation the server stays on the last map, answering getstatus
every five minutes, until the time is up; then it is told to quit, and the
log is searched for sanitizer reports.

Exit status 0 means every map loaded, every query was answered, the server
was alive at the end and quit cleanly, and the log has no sanitizer report.

usage: g2_run.py --server bin/KisakCOD-dedi --basepath <Steam 1.8 install>
                 --work <scratch dir> [--port 28960] [--duration 1800]
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import threading
import time

ROTATION = ["mp_shipment", "mp_vacant", "mp_crash", "mp_backlot", "mp_overgrown"]
GAMETYPE = "war"
READY = r"Hitch warning|Huffman Took"
SANITIZER = r"ERROR: AddressSanitizer.*|runtime error:.*|ERROR: LeakSanitizer.*"


class Server:
    """The server process, its console log and the lines read so far."""

    def __init__(self, args):
        env = dict(os.environ)
        env.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
        env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1")
        home = os.path.join(args.work, "home")
        os.makedirs(home, exist_ok=True)
        command = [args.server, "+set", "fs_basepath", args.basepath, "+set", "fs_homepath", home,
                   "+set", "db_unverified64", "1", "+set", "sv_maxclients", "8",
                   "+set", "net_port", str(args.port), "+map", ROTATION[0]]
        self.log = open(os.path.join(args.work, "server.log"), "w")
        self.lines = []
        self.lock = threading.Lock()
        self.proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, env=env, text=True, bufsize=1)
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        for line in self.proc.stdout:
            self.log.write(line)
            self.log.flush()
            with self.lock:
                self.lines.append(line)

    def mark(self):
        with self.lock:
            return len(self.lines)

    def wait_for(self, pattern, after, timeout):
        deadline = time.time() + timeout
        rx = re.compile(pattern)
        while time.time() < deadline and self.alive():
            with self.lock:
                if any(rx.search(line) for line in self.lines[after:]):
                    return True
            time.sleep(1)
        return False

    def command(self, text):
        self.proc.stdin.write(text + "\n")
        self.proc.stdin.flush()

    def alive(self):
        return self.proc.poll() is None


def query(port, what):
    """One out-of-band query; the reply without its 0xFFFFFFFF prefix, or None."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(3)
        try:
            sock.sendto(b"\xff\xff\xff\xff" + what.encode() + b" g2", ("127.0.0.1", port))
            data, _ = sock.recvfrom(65536)
        except OSError:
            return None
    return data[4:].decode("latin-1", "replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--server", required=True, help="the headless server binary")
    parser.add_argument("--basepath", required=True, help="the Steam 1.8 install (fs_basepath)")
    parser.add_argument("--work", required=True, help="scratch directory for fs_homepath and the logs")
    parser.add_argument("--port", type=int, default=28960)
    parser.add_argument("--duration", type=int, default=1800, help="seconds to run in all")
    args = parser.parse_args()
    os.makedirs(args.work, exist_ok=True)

    server = Server(args)
    replies = open(os.path.join(args.work, "queries.txt"), "w")
    start = time.time()
    failures = []

    def elapsed():
        return time.time() - start

    def probe(where, kinds):
        for kind in kinds:
            reply = query(args.port, kind)
            replies.write("== %s on %s at %.0fs ==\n%s\n" % (kind, where, elapsed(), reply))
            replies.flush()
            if reply is None:
                failures.append("no %s reply on %s at %.0fs" % (kind, where, elapsed()))

    if not server.wait_for(READY, 0, 600):
        failures.append("%s did not come up" % ROTATION[0])
    else:
        print("%s up at %.0fs" % (ROTATION[0], elapsed()), flush=True)
        rotation = "gametype %s " % GAMETYPE + " ".join("map " + name for name in ROTATION)
        rest = "gametype %s " % GAMETYPE + " ".join("map " + name for name in ROTATION[1:])
        server.command('set sv_mapRotation "%s"' % rotation)
        server.command('set sv_mapRotationCurrent "%s"' % rest)
        for index, name in enumerate(ROTATION):
            if index:
                mark = server.mark()
                server.command("map_rotate")
                if not (server.wait_for(r"Headless dedicated loading map '%s'" % name, mark, 600)
                        and server.wait_for(READY, mark, 900)):
                    failures.append("%s did not come up" % name)
                    break
                print("%s up at %.0fs" % (name, elapsed()), flush=True)
            time.sleep(5)
            probe(name, ("getinfo", "getstatus"))

    while server.alive() and not failures and elapsed() < args.duration:
        time.sleep(min(300, max(1, args.duration - elapsed())))
        if server.alive():
            probe("the last map", ("getstatus",))

    alive = server.alive()
    if not alive:
        failures.append("the server exited before the end, status %s" % server.proc.returncode)
    else:
        server.command("quit")
        try:
            server.proc.wait(120)
        except subprocess.TimeoutExpired:
            server.proc.terminate()
            server.proc.wait(30)
            failures.append("the server did not quit")
        if server.proc.returncode != 0:
            failures.append("the server quit with status %s" % server.proc.returncode)
    time.sleep(1)
    server.log.close()
    replies.close()

    with open(os.path.join(args.work, "server.log"), errors="replace") as log:
        reports = re.findall(SANITIZER, log.read())
    failures += ["sanitizer: " + report for report in reports[:5]]
    print("ran %.0fs; %d query replies; %d sanitizer reports" %
          (elapsed(), open(os.path.join(args.work, "queries.txt")).read().count("Response"), len(reports)))
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
