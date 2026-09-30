#!/usr/bin/env python3
"""Assetless smoke run of the POSIX headless dedicated server (NOW row 13).

Retail Steam 1.8 data never reaches CI (docs/ROADMAP.md). The server is started
against two synthetic stand-in files, the ones Com_Init reads before any fast
file: a non-empty fileSysCheck.cfg and a minimal configure_mp.csv. With them it
must get through the filesystem, console, dvar and autoconfigure stages and
then stop on the first fast file, which the 64-bit loader cannot load before
G2. The database thread's fatal error races the rest of Com_Init (network,
"Common Initialization Complete"), so only the deterministic stages are
checked.

The base path contains a space, so the command-line quoting is exercised too.
Exit status 0 means every expectation held; otherwise each broken one is
printed together with the server's output.

usage: headless_smoke.py path/to/KisakCOD-dedi
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Shaped like retail's: CPU rows with real thresholds (the 100 GHz row must not
# fit), and a GPU table with no row for a headless host.
CONFIGURE_CSV = ('cpu ghz,sys mb,kisak_smoke_cpu\n1.0,256,1\n100.0,4096,2\n'
                 'gpu,kisak_smoke_gpu\n*GeForce 8800*,1\n')


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    server = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='kisak smoke ') as base:
        main_dir = Path(base) / 'main'
        main_dir.mkdir()
        (main_dir / 'fileSysCheck.cfg').write_text('// synthetic stand-in (CI never sees retail data)\n')
        (main_dir / 'configure_mp.csv').write_text(CONFIGURE_CSV)
        try:
            homepath = Path(base) / 'home dir' / 'nested'   # does not exist yet
            run = subprocess.run([str(server), '+set', 'fs_basepath', base, '+set', 'fs_homepath', str(homepath),
                                  '+set', 'dedicated', '1'],
                                 stdin=subprocess.DEVNULL, capture_output=True, text=True, errors='replace',
                                 timeout=120, cwd=base)
        except subprocess.TimeoutExpired as exc:
            print(f'FAIL the server did not exit within 120 s\n{exc.stdout or ""}{exc.stderr or ""}')
            return 1
        log = run.stdout + run.stderr
        zone = re.search(r"Could not find zone '([^']*)'", log)
        checks = [
            (re.search(r'build (linux|macos)-(x64|arm64)', log), 'prints a POSIX build banner'),
            (log.count('begin $init') == 1, 'prints each line once'),
            (base + '/main' in log, 'roots the filesystem at the (spaced) fs_basepath'),
            ('configure_mp.csv: using CPU configuration 1 GHz 256 MB' in log, 'picks the CPU row that fits the host'),
            ('no GPU row fits "headless"' in log, 'keeps defaults when no GPU row fits'),
            ((homepath / 'main' / 'console_mp.log').is_file(), 'creates the nested fs_homepath for its log'),
            ('Unknown command' not in log, 'runs no stray command-line token'),
            ('Loading fastfile code_post_gfx_mp' in log, 'starts the first fast-file load'),
            (zone and '\\' not in zone.group(1) and zone.group(1).startswith(base + '/zone/'),
             'looks for fast files under <basepath>/zone/ with / separators'),
            (run.returncode == 1, f'exits with status 1 on the missing fast file (got {run.returncode})'),
        ]
        failed = [name for ok, name in checks if not ok]
        for ok, name in checks:
            print(f"{'PASS' if ok else 'FAIL'}  {name}")
        if failed:
            print('\n--- server output ---\n' + log)
            return 1
        return 0


if __name__ == '__main__':
    sys.exit(main())
