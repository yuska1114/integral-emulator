# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess
import sys
import tempfile

def run_test(arguments, directory):
    result = subprocess.run(arguments, cwd=directory, capture_output=True,
                            env={**os.environ, 'SDL_VIDEODRIVER': 'dummy'})
    for output, stream in ((result.stdout, sys.stdout), (result.stderr, sys.stderr)):
        if output:
            print(output.decode('utf-8', errors='replace'), end='', file=stream, flush=True)
    # Some SDL Windows builds redirect standard streams into the test directory.
    # Preserve diagnostics before TemporaryDirectory removes it, including on failure.
    for name in ('stdout.txt', 'stderr.txt'):
        path = Path(directory) / name
        if path.is_file():
            print(f'{name}:\n{path.read_text(encoding="utf-8", errors="replace")}',
                  file=sys.stderr, flush=True)
    result.check_returncode()

if sys.argv[1] == '--n64':
    binary = str(Path(sys.argv[2]).resolve())
    mask = os.umask(0o027)
    with tempfile.TemporaryDirectory(prefix='integral-n64-capture-') as directory:
        run_test([binary, '--capture-unit', str(Path(directory) / 'media.ipc')], directory)
        captures = list((Path(directory) / 'screenshot').glob('*.bmp'))
        assert len(captures) == 2
        import struct
        for capture in captures:
            data = capture.read_bytes()
            assert data[:2] == b'BM' and len(data) == struct.unpack_from('<I', data, 2)[0]
            assert struct.unpack_from('<ii', data, 18) == (160, 144)
            if os.name != 'nt':
                assert capture.stat().st_mode & 0o777 == 0o640
        if os.name != 'nt':
            assert (Path(directory) / 'screenshot').stat().st_mode & 0o777 == 0o750
    os.umask(mask)
    print('Remote keyboard/controller capture: exactly two valid BMPs, existing permissions PASS')
    sys.exit(0)

for argument, expected in zip(sys.argv[1:], [1, 2]):
    binary = str(Path(argument).resolve())
    with tempfile.TemporaryDirectory(prefix="integral-room-util-") as directory:
        run_test([binary], directory)
        assert len(list((Path(directory) / "screenshot").glob("*.bmp"))) == expected
