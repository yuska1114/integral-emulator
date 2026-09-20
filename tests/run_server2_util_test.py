# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise actual SERVER2 main loop with disposable saves and a supplied ROM."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, rom = (str(Path(item).resolve()) for item in sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="integral-server2-util-") as directory:
    subprocess.run([binary, "--rom1", rom, "--rom2", rom,
                    "--save1", "slot1.sav", "--save2", "slot2.sav",
                    "--display", "--no-audio", "--no-link", "--frames", "4"],
                   cwd=directory, check=True,
                   env={**os.environ, "SDL_VIDEODRIVER": "dummy",
                        "SDL_AUDIODRIVER": "dummy"})
    files = list((Path(directory) / "screenshot").glob("local_gb_server2_local_*.bmp"))
    assert len(files) == 1
