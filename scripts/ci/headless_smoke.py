#!/usr/bin/env python3
"""Assetless smoke run of the headless dedicated server (NOW row 13, gate G1).

Retail Steam 1.8 data never reaches CI (docs/ROADMAP.md). The server is started
against two synthetic stand-in files, the ones Com_Init reads before any fast
file: a non-empty fileSysCheck.cfg and a minimal configure_mp.csv. With them it
must get through the filesystem, console, dvar and autoconfigure stages and
then stop on the first fast file, which the 64-bit loader cannot load before
G2. The database thread's fatal error races the rest of Com_Init (network,
"Common Initialization Complete"), so only the deterministic stages are
checked. There is no localization.txt either, as for a server started outside
its install directory, so the fast files are looked for in the English zones.

The base path contains a space, so the command-line quoting is exercised too.
A second run claims an archived machine profile that no longer matches the
host (com_recommendedSet 1, sys_configureGHz 999): a headless server must give
the non-interactive answer and keep starting, not wait on a question nobody can
see.
A non-empty main/english folder makes the filesystem probe it
(Sys_DirectoryHasContents) and add it as the localized folder.
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
import threading
from pathlib import Path

from synthetic_zones import write_init_zones

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


def is_server(path):
    """Only ever the server binary this job built: an existing executable file."""
    return path.name == EXE and path.is_file() and os.access(path, os.X_OK)


def run_on_console(server, args, cwd):
    """Return (exit status, console text) of the server started on a console of its own, as from cmd."""
    if sys.platform != 'win32':
        raise OSError('only Windows gives a process a console of its own')
    # This script re-runs itself (--on-console) on a new hidden console and starts
    # the server there with nothing redirected; then it reads that console back.
    screen = Path(cwd) / 'console screen.txt'
    hidden = subprocess.STARTUPINFO(dwFlags=subprocess.STARTF_USESHOWWINDOW, wShowWindow=0)
    subprocess.run([sys.executable, str(Path(__file__).resolve()), '--on-console', str(screen), str(server), *args],
                   check=False, creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=hidden, timeout=150, cwd=cwd)
    text = screen.read_text(encoding='utf-8') if screen.is_file() else 'no console record\n'
    status, _, shown = text.partition('\n')
    return status, shown


def on_console(screen, server, *args):
    """--on-console: run the server on this console; save its exit status and the console's text."""
    if sys.platform != 'win32':
        return 2
    import ctypes
    from ctypes import wintypes

    class Coord(ctypes.Structure):
        _fields_ = [('X', wintypes.SHORT), ('Y', wintypes.SHORT)]

    class ScreenInfo(ctypes.Structure):
        _fields_ = [('size', Coord), ('cursor', Coord), ('attributes', wintypes.WORD),
                    ('window', wintypes.SMALL_RECT), ('maximum', Coord)]

    server = Path(server).resolve()
    if not is_server(server):
        Path(screen).write_text(f'none\nnot the {EXE} executable: {server}\n', encoding='utf-8')
        return 2
    try:
        status = subprocess.run([str(server), *args], check=False, timeout=120).returncode
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
        text = cells.value[:read.value]
        rows = [text[i:i + width].rstrip() for i in range(0, len(text), width)]
        Path(screen).write_text(f'{status}\n' + '\n'.join(rows), encoding='utf-8')
    except (OSError, subprocess.SubprocessError) as exc:  # the hidden console would swallow a traceback
        Path(screen).write_text(f'none\nconsole probe failed: {exc!r}\n', encoding='utf-8')
    return 0


INIT_COMPLETE = '--- Common Initialization Complete ---'


def quit_run(server, base, zone_dir):
    """Return (checks, log) of a `quit` typed once Com_Init completes with empty init zones.

    The database's shutdown used to raise an error while holding its hash lock, and
    the error's localized-message lookup waited on that lock: every orderly quit hung.
    """
    write_init_zones(zone_dir)
    home = Path(base) / 'quit home'  # fresh: nothing the first run archived
    # An argument list with no shell; main() admits only the server this job built.
    # nosemgrep
    server_run = subprocess.Popen([str(server), '+set', 'fs_basepath', base,  # nosec B603
                                   '+set', 'fs_homepath', str(home), '+set', 'dedicated', '1'],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, errors='replace', cwd=base)
    commands, output = server_run.stdin, server_run.stdout
    if commands is None or output is None:
        raise OSError('the server has no stdin or stdout pipe')
    lines, settled = [], threading.Event()

    def read():
        for line in output:
            lines.append(line)
            if INIT_COMPLETE in line:
                settled.set()
        settled.set()

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    settled.wait(120)
    started = any(INIT_COMPLETE in line for line in lines)
    status = 'no quit sent'
    if started and server_run.poll() is None:
        try:
            commands.write('quit\n')
            commands.flush()
            status = server_run.wait(timeout=30)
        except (OSError, subprocess.TimeoutExpired) as exc:
            status = f'no exit ({type(exc).__name__})'
    if server_run.poll() is None:
        server_run.kill()
    server_run.wait()
    reader.join(10)
    log = ''.join(lines)
    return [
        (started, 'completes Com_Init with empty init zones'),
        ('Unloaded fastfile code_post_gfx_mp' in log, 'unloads its zones on a typed quit'),
        (status == 0, f'exits with status 0 within 30 s of a typed quit (got {status})'),
    ], log


def main():
    if len(sys.argv) > 3 and sys.argv[1] == '--on-console':
        return on_console(*sys.argv[2:])
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    server = Path(sys.argv[1]).resolve()
    if not is_server(server):
        print(f'not the {EXE} executable: {server}')
        return 2
    with tempfile.TemporaryDirectory(prefix='kisak smoke ') as base:
        main_dir = Path(base) / 'main'
        main_dir.mkdir()
        (main_dir / 'fileSysCheck.cfg').write_text('// synthetic stand-in (CI never sees retail data)\n')
        (main_dir / 'configure_mp.csv').write_text(CONFIGURE_CSV)
        (main_dir / 'english').mkdir()
        (main_dir / 'english' / 'localized_stand_in.txt').write_text('synthetic\n')
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
            (re.search(BANNER, log), 'prints the build banner for this OS'),
            (log.count('begin $init') == 1, 'prints each line once'),
            (base + '/main' in log, 'roots the filesystem at the (spaced) fs_basepath'),
            (base + '/main/english' in log and 'localized assets game folder for english' in log,
             'adds the non-empty main/english folder as the localized folder'),
            ('configure_mp.csv: using CPU configuration 1 GHz 256 MB' in log, 'picks the CPU row that fits the host'),
            (f'no GPU row fits "{GPU}"' in log, 'keeps defaults when no GPU row fits'),
            ((homepath / 'main' / 'console_mp.log').is_file(), 'creates the nested fs_homepath for its log'),
            ('Unknown command' not in log, 'runs no stray command-line token'),
            ('Loading fastfile code_post_gfx_mp' in log, 'starts the first fast-file load'),
            (zone and ('/' if SEP == '\\' else '\\') not in zone.group(1)
             and zone.group(1).startswith(SEP.join((base, 'zone', 'english', ''))),
             f'looks for fast files under <basepath>{SEP}zone{SEP}english{SEP} with {SEP} separators'),
            (run.returncode == 1, f'exits with status 1 on the missing fast file (got {run.returncode})'),
        ]
        # Fresh fs_homepath: an archived config from the first run would override
        # the +set values before the check reads them.
        changed_home = Path(base) / 'changed home'
        try:
            changed = subprocess.run([str(server), '+set', 'fs_basepath', base, '+set', 'fs_homepath',
                                      str(changed_home), '+set', 'dedicated', '1', '+set', 'com_recommendedSet', '1',
                                      '+set', 'sys_configureGHz', '999'],
                                     stdin=subprocess.DEVNULL, capture_output=True, text=True, errors='replace',
                                     timeout=120, cwd=base)
            changed_status = changed.returncode
            log += '\n--- the changed-machine run ---\n' + changed.stdout + changed.stderr
        except subprocess.TimeoutExpired:
            changed_status = 'no exit within 120 s'
        checks.append((changed_status == 1,
                       f'starts on a changed machine without asking, then exits with status 1 (got {changed_status})'))
        if os.name == 'nt':
            status, screen = run_on_console(server, run.args[1:], base)
            log += '\n--- the console it was started on ---\n' + screen
            checks += [
                (pe_subsystem(server) == CONSOLE_SUBSYSTEM, 'is a console-subsystem program'),
                (re.search(BANNER, screen), 'prints to the console it is started on'),
                (status == '1', f'exits with status 1 there too (got {status})'),
            ]
        # The zones go where the first run looked for them.
        zone_dir = Path(zone.group(1)).parent if zone else Path(base) / 'zone' / 'english'
        quit_checks, quit_log = quit_run(server, base, zone_dir)
        checks += quit_checks
        log += '\n--- the quit run ---\n' + quit_log
        failed = [name for ok, name in checks if not ok]
        for ok, name in checks:
            print(f"{'PASS' if ok else 'FAIL'}  {name}")
        if failed:
            print('\n--- server output ---\n' + log)
            return 1
        return 0


if __name__ == '__main__':
    sys.exit(main())
