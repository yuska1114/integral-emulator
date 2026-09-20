# SPDX-License-Identifier: GPL-3.0-or-later
"""Run synthetic capture regression without touching user screenshots."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="integral-util-") as directory:
    subprocess.run([binary], cwd=directory, check=True,
                   env={**os.environ, "SDL_VIDEODRIVER": "dummy"})
    assert len(list((Path(directory) / "screenshot").glob("*.bmp"))) == 12
