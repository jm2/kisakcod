#!/usr/bin/env python3
"""Run the G2 check (docs/NOW.md row 45) on the 64-bit headless server.

The server, built with ASan and UBSan, boots mp_shipment (db_unverified64),
answers getinfo and getstatus, rotates the five G2 maps and runs for the given
time.

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
import subprocess  # nosec B404 - runs only the operator's own server binary with a fixed argv
import sys
import threading
import time

ROTATION = ["mp_shipment", "mp_vacant", "mp_crash", "mp_backlot", "mp_overgrown"]
GAMETYPE = "war"
READY = r"Hitch warning|Huffman Took"
SANITIZER = r"ERROR: AddressSanitizer.*|runtime error:.*|ERROR: LeakSanitizer.*"
SUMMARY = "Run the G2 check (docs/NOW.md row 45) on the 64-bit headless server."


class Server:
    """The server process, its console log and the lines read so far."""

    def __init__(self, args):
        """Start the server on the rotation's first map, logging to the work directory."""
        env = dict(os.environ)
        env.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
        env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1")
        home = os.path.join(args.work, "home")
        os.makedirs(home, exist_ok=True)
        command = [args.server, "+set", "fs_basepath", args.basepath, "+set", "fs_homepath", home,
                   "+set", "db_unverified64", "1", "+set", "sv_maxclients", "8",
                   "+set", "net_port", str(args.port), "+map", ROTATION[0]]
        self.log_path = os.path.join(args.work, "server.log")
        self.lines = []
        self.lock = threading.Lock()
        # The argv is fixed apart from the operator's own server path and install path.
        self.proc = subprocess.Popen(  # nosec B603  # nosemgrep
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            env=env, text=True, bufsize=1)
        if self.proc.stdin is None or self.proc.stdout is None:
            raise RuntimeError("the server's console pipes did not open")
        self.stdin = self.proc.stdin
        self.stdout = self.proc.stdout
        self.pump = threading.Thread(target=self._pump, daemon=True)
        self.pump.start()

    def _pump(self):
        with open(self.log_path, "w") as log:
            for line in self.stdout:
                log.write(line)
                log.flush()
                with self.lock:
                    self.lines.append(line)

    def mark(self):
        """Return the number of console lines read so far."""
        with self.lock:
            return len(self.lines)

    def wait_for(self, pattern, after, timeout):
        """Wait until a console line after `after` matches `pattern`."""
        deadline = time.time() + timeout
        rx = re.compile(pattern)
        while time.time() < deadline and self.alive():
            with self.lock:
                if any(rx.search(line) for line in self.lines[after:]):
                    return True
            time.sleep(1)
        return False

    def command(self, text):
        """Send one line to the server console."""
        self.stdin.write(text + "\n")
        self.stdin.flush()

    def alive(self):
        """Return whether the server is still running."""
        return self.proc.poll() is None


def query(port, what):
    """Send one out-of-band query; return the reply without its 0xFFFFFFFF prefix, or None."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(3)
        try:
            sock.sendto(b"\xff\xff\xff\xff" + what.encode() + b" g2", ("127.0.0.1", port))
            data, _ = sock.recvfrom(65536)
        except OSError:
            return None
    return data[4:].decode("latin-1", "replace")


class Run:
    """One G2 run: its server, its reply log and the failures it found."""

    def __init__(self, args):
        """Start the server and open the reply log."""
        self.args = args
        self.start = time.time()
        self.failures = []
        self.reply_count = 0
        self.replies = open(os.path.join(args.work, "queries.txt"), "w")  # closed in finish()
        self.server = Server(args)

    def elapsed(self):
        """Return the seconds since the run started."""
        return time.time() - self.start

    def probe(self, where, kinds):
        """Query each of `kinds` and record the replies."""
        for kind in kinds:
            reply = query(self.args.port, kind)
            self.replies.write("== %s on %s at %.0fs ==\n%s\n" % (kind, where, self.elapsed(), reply))
            self.replies.flush()
            if reply is None:
                self.failures.append("no %s reply on %s at %.0fs" % (kind, where, self.elapsed()))
            else:
                self.reply_count += 1

    def rotate(self):
        """Boot the first map, set the G2 rotation and walk it, probing each map."""
        if not self.server.wait_for(READY, 0, 600):
            self.failures.append("%s did not come up" % ROTATION[0])
            return
        print("%s up at %.0fs" % (ROTATION[0], self.elapsed()), flush=True)
        maps = " ".join("map " + name for name in ROTATION)
        rest = " ".join("map " + name for name in ROTATION[1:])
        self.server.command('set sv_mapRotation "gametype %s %s"' % (GAMETYPE, maps))
        self.server.command('set sv_mapRotationCurrent "gametype %s %s"' % (GAMETYPE, rest))
        for index, name in enumerate(ROTATION):
            if index and not self.next_map(name):
                return
            time.sleep(5)
            self.probe(name, ("getinfo", "getstatus"))

    def next_map(self, name):
        """Rotate to `name` and wait until it is up."""
        mark = self.server.mark()
        self.server.command("map_rotate")
        if self.server.wait_for(r"Headless dedicated loading map '%s'" % name, mark, 600) \
                and self.server.wait_for(READY, mark, 900):
            print("%s up at %.0fs" % (name, self.elapsed()), flush=True)
            return True
        self.failures.append("%s did not come up" % name)
        return False

    def soak(self):
        """Stay on the last map until the duration is up, probing getstatus every five minutes."""
        while self.server.alive() and not self.failures and self.elapsed() < self.args.duration:
            time.sleep(min(300, max(1, self.args.duration - self.elapsed())))
            if self.server.alive():
                self.probe("the last map", ("getstatus",))

    def finish(self):
        """Quit the server, close the logs and record how it ended."""
        proc = self.server.proc
        if not self.server.alive():
            self.failures.append("the server exited before the end, status %s" % proc.returncode)
        else:
            self.server.command("quit")
            try:
                proc.wait(120)
            except subprocess.TimeoutExpired:
                proc.terminate()
                proc.wait(30)
                self.failures.append("the server did not quit")
            if proc.returncode != 0:
                self.failures.append("the server quit with status %s" % proc.returncode)
        self.server.pump.join(30)
        self.replies.close()

    def report(self):
        """Search the console log for sanitizer reports, print the result and return the exit status."""
        with open(self.server.log_path, errors="replace") as log:
            reports = re.findall(SANITIZER, log.read())
        self.failures += ["sanitizer: " + report for report in reports[:5]]
        print("ran %.0fs; %d query replies; %d sanitizer reports"
              % (self.elapsed(), self.reply_count, len(reports)))
        for failure in self.failures:
            print("FAIL:", failure)
        return 1 if self.failures else 0


def main():
    """Parse the arguments and run the G2 check."""
    parser = argparse.ArgumentParser(description=SUMMARY)
    parser.add_argument("--server", required=True, help="the headless server binary")
    parser.add_argument("--basepath", required=True, help="the Steam 1.8 install (fs_basepath)")
    parser.add_argument("--work", required=True, help="scratch directory for fs_homepath and the logs")
    parser.add_argument("--port", type=int, default=28960)
    parser.add_argument("--duration", type=int, default=1800, help="seconds to run in all")
    args = parser.parse_args()
    os.makedirs(args.work, exist_ok=True)

    run = Run(args)
    run.rotate()
    run.soak()
    run.finish()
    return run.report()


if __name__ == "__main__":
    sys.exit(main())
