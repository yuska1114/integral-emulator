#!/usr/bin/env python3
"""Run memory/IPC tests in disposable storage; no SAV may be created."""
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="integral-transfer-memory-") as directory:
    subprocess.run([str(pathlib.Path(sys.argv[1]).resolve()), directory], check=True, timeout=40)
    assert {p.name for p in pathlib.Path(directory).iterdir()} == {"slot1.gbc", "slot2.gbc"}
    process = subprocess.Popen([str(pathlib.Path(sys.argv[1]).resolve()), "--hold", directory],
                               stdout=subprocess.PIPE, text=True)
    try:
        assert process.stdout.readline().strip() == "MEMORY READY"
        assert {p.name for p in pathlib.Path(directory).iterdir()} == {"slot1.gbc", "slot2.gbc"}
    finally:
        process.kill()
        process.wait(timeout=5)
    assert {p.name for p in pathlib.Path(directory).iterdir()} == {"slot1.gbc", "slot2.gbc"}
print("No GB SAV/RTC/part files produced PASS")
print("Forced process exit with live SAV buffers leaves no SAV file PASS")
