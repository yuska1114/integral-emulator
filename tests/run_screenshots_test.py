# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the viewer regression with disposable, synthetic images only."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="integral-screenshots-") as directory:
    subprocess.run([str(Path(sys.argv[1]).resolve()), directory],
                   env={**os.environ, "SDL_VIDEODRIVER": "dummy"}, check=True)
