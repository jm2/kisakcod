#!/usr/bin/env python3
"""Assetless smoke run of the headless dedicated server (NOW row 13, gate G1).

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

The same checks run on Windows against KisakCOD-dedi.exe, where the build
banner, the GPU description and the fast-file path separator are the Windows
ones. The Windows headless server writes its console to the inherited standard
handles, so the redirected output is the whole log there too. On Windows the
server must also be a console program: its PE header names the console
subsystem, and started on a console of its own with nothing redirected, as from
cmd, it prints to that console.

usage: headless_smoke.py path/to/KisakCOD-dedi[.exe]
"""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Shaped like retail's: CPU rows with real thresholds (the 100 GHz row must not
# fit), and a GPU table with no row for a headless host.
CONFIGURE_CSV = ('cpu ghz,sys mb,kisak_smoke_cpu\n1.0,256,1\n100.0,4096,2\n'
                 'gpu,kisak_smoke_gpu\n*GeForce 8800*,1\n')

# What differs per OS: the binary's name, the CPUSTRING in the build banner, the
# GPU description Sys_FindInfo reports, and the fast-file path separator.
if os.name == 'nt':
    EXE, BANNER, GPU, SEP = 'KisakCOD-dedi.exe', r'build win-(x86|x64|arm64)\b', 'Headless dedicated server', '\\'
else:
    EXE, BANNER, GPU, SEP = 'KisakCOD-dedi', r'build (linux|macos)-(x64|arm64)\b', 'headless', '/'

CONSOLE_SUBSYSTEM = 3  # IMAGE_SUBSYSTEM_WINDOWS_CUI


def pe_subsystem(exe):
    """The Subsystem field of a PE image's optional header, or None if exe is no PE image."""
    with open(exe, 'rb') as image:
        head = image.read(4096)
    pe = int.from_bytes(head[0x3C:0x40], 'little')
    if head[pe:pe + 4] != b'PE\0\0':
        return None
    # The optional header follows the 4-byte signature and the 20-byte file
    # header; Subsystem sits at offset 68 in both PE32 and PE32+.
    return int.from_bytes(head[pe + 92:pe + 94], 'little')


def run_on_console(server, args, cwd):
    """(exit status, console text) of the server started on a console of its own.

    This script runs on a new hidden console (--on-console) and starts the server
    there with nothing redirected, as cmd does; then it reads the console back.
    """
    screen = Path(cwd) / 'console screen.txt'
    hidden = subprocess.STARTUPINFO(dwFlags=subprocess.STARTF_USESHOWWINDOW, wShowWindow=0)
    subprocess.run([sys.executable, str(Path(__file__).resolve()), '--on-console', str(screen), str(server), *args],
                   creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=hidden, timeout=150, cwd=cwd)
    text = screen.read_text(encoding='utf-8') if screen.is_file() else 'no console record\n'
    status, _, shown = text.partition('\n')
    return status, shown


def on_console(screen, server, *args):
    """--on-console: run the server on this console; save its exit status and the console's text."""
    import ctypes
    from ctypes import wintypes

    class Coord(ctypes.Structure):
        _fields_ = [('X', wintypes.SHORT), ('Y', wintypes.SHORT)]

    class ScreenInfo(ctypes.Structure):
        _fields_ = [('size', Coord), ('cursor', Coord), ('attributes', wintypes.WORD),
                    ('window', wintypes.SMALL_RECT), ('maximum', Coord)]

    try:
        status = subprocess.run([server, *args], timeout=120).returncode
        kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel32.GetStdHandle.restype = wintypes.HANDLE
        kernel32.GetConsoleScreenBufferInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(ScreenInfo)]
        kernel32.ReadConsoleOutputCharacterW.argtypes = [
            wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, Coord, ctypes.POINTER(wintypes.DWORD)]
        output = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
        info = ScreenInfo()
        if not kernel32.GetConsoleScreenBufferInfo(output, ctypes.byref(info)):
            raise OSError(ctypes.get_last_error(), 'GetConsoleScreenBufferInfo')
        width = info.size.X
        cells = ctypes.create_unicode_buffer(width * (info.cursor.Y + 1))
        read = wintypes.DWORD()
        if not kernel32.ReadConsoleOutputCharacterW(output, cells, len(cells), Coord(0, 0),
                                                    ctypes.byref(read)):
            raise OSError(ctypes.get_last_error(), 'ReadConsoleOutputCharacterW')
        rows = [cells[i:i + width].rstrip() for i in range(0, read.value, width)]
        Path(screen).write_text(f'{status}\n' + '\n'.join(rows), encoding='utf-8')
    except Exception as exc:  # the hidden console would swallow a traceback
        Path(screen).write_text(f'none\nconsole probe failed: {exc!r}\n', encoding='utf-8')
    return 0


def main():
    if len(sys.argv) > 3 and sys.argv[1] == '--on-console':
        return on_console(*sys.argv[2:])
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    server = Path(sys.argv[1]).resolve()
    # Only ever the server binary this job built: an existing executable file.
    if server.name != EXE or not server.is_file() or not os.access(server, os.X_OK):
        print(f'not the {EXE} executable: {server}')
        return 2
    with tempfile.TemporaryDirectory(prefix='kisak smoke ') as base:
        main_dir = Path(base) / 'main'
        main_dir.mkdir()
        (main_dir / 'fileSysCheck.cfg').write_text('// synthetic stand-in (CI never sees retail data)\n')
        (main_dir / 'configure_mp.csv').write_text(CONFIGURE_CSV)
        try:
            homepath = Path(base) / 'home dir' / 'nested'   # does not exist yet
            args = ['+set', 'fs_basepath', base, '+set', 'fs_homepath', str(homepath), '+set', 'dedicated', '1']
            run = subprocess.run([str(server), *args],
                                 stdin=subprocess.DEVNULL, capture_output=True, text=True, errors='replace',
                                 timeout=120, cwd=base)
        except subprocess.TimeoutExpired as exc:
            print(f'FAIL the server did not exit within 120 s\n{exc.stdout or ""}{exc.stderr or ""}')
            return 1
        log = run.stdout + run.stderr
        zone = re.search(r"Could not find zone '([^']*)'", log)
        checks = [
            (re.search(BANNER, log), 'prints the build banner for this OS'),
            (log.count('begin $init') == 1, 'prints each line once'),
            (base + '/main' in log, 'roots the filesystem at the (spaced) fs_basepath'),
            ('configure_mp.csv: using CPU configuration 1 GHz 256 MB' in log, 'picks the CPU row that fits the host'),
            (f'no GPU row fits "{GPU}"' in log, 'keeps defaults when no GPU row fits'),
            ((homepath / 'main' / 'console_mp.log').is_file(), 'creates the nested fs_homepath for its log'),
            ('Unknown command' not in log, 'runs no stray command-line token'),
            ('Loading fastfile code_post_gfx_mp' in log, 'starts the first fast-file load'),
            (zone and ('/' if SEP == '\\' else '\\') not in zone.group(1)
             and zone.group(1).startswith(base + SEP + 'zone' + SEP),
             f'looks for fast files under <basepath>{SEP}zone{SEP} with {SEP} separators'),
            (run.returncode == 1, f'exits with status 1 on the missing fast file (got {run.returncode})'),
        ]
        if os.name == 'nt':
            status, screen = run_on_console(server, args, base)
            log += '\n--- the console it was started on ---\n' + screen
            checks += [
                (pe_subsystem(server) == CONSOLE_SUBSYSTEM, 'is a console-subsystem program'),
                (re.search(BANNER, screen), 'prints to the console it is started on'),
                (status == '1', f'exits with status 1 there too (got {status})'),
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
