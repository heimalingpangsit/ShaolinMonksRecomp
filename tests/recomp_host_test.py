"""Behavior checks for the separate Windows x64 recompilation host."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    exe = args.exe.resolve()
    assert exe.is_file(), "The recompiled host executable has not been built"
    image = exe.read_bytes()
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    assert image[pe:pe + 4] == b"PE\0\0"
    assert struct.unpack_from("<H", image, pe + 4)[0] == 0x8664, "Host must be AMD64"

    def run(*arguments, expected=0):
        result = subprocess.run(
            [str(exe), *map(str, arguments)], capture_output=True, text=True,
            timeout=20, creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS,
        )
        assert result.returncode == expected, (arguments, result.returncode, result.stdout[-2000:], result.stderr[-2000:])
        return result

    info = json.loads(run("--info").stdout)
    assert info["host_bits"] == 64 and info["entry"] == "0x000F40CA"
    assert info["title_id"] == "0x4D570029" and info["mapped_functions"] > 0
    run("--portable-saves", "--info")
    with tempfile.TemporaryDirectory(prefix="shaolin-save-cli-") as saves:
        run("--save-dir", saves, "--info")
        run("--portable-saves", "--save-dir", saves, expected=2)
    run("--save-dir", expected=2)
    run("--unknown", expected=2)
    run("--background", "--timeout", "invalid", expected=2)
    run("--background", "--timeout", "0", expected=2)
    with tempfile.TemporaryDirectory(prefix="shaolin-system-log-") as folder:
        hidden = json.loads(run("--background", "--host-test", "--data-dir", folder).stdout)
        logs = list((Path(folder) / "logs").glob("*.log"))
        assert len(logs) == 1, logs
        session = logs[0].read_text(encoding="utf-8", errors="replace")
        assert re.search(r"Shaolin Monks Recomp version: \d+\.\d+\.\d+", session), session[-2000:]
        for field in ("OS: ", "CPU: ", "RAM: ", "Desktop: ", "Settings:"):
            assert field in session, (field, session[-2000:])
    assert hidden == {"background": True, "visible": False, "display_changed": False,
                      "isolated_input": True, "keyboard_messages": True}, hidden
    run("--host-test", expected=2)
    for count in ("0", "4", "-1", "invalid"):
        run("--background", "--virtual-gamepads", count, expected=2)
    run("--virtual-gamepads", "1", expected=2)
    run("--background", "--host-test", "--virtual-gamepads", "1", expected=2)
    with tempfile.TemporaryDirectory(prefix="shaolin-recomp-negative-") as folder:
        run("--background", "--game", folder, expected=3)
        Path(folder, "default.xbe").write_bytes(b"XBEH-invalid-fixture")
        rejected = run("--background", "--game", folder, expected=3)
        assert "not supported" in rejected.stderr
        run("--background", "--disc", Path(folder, "invalid.iso"), expected=3)
    print("PASS: AMD64 identity, strict CLI, hidden window and rejected game files")


if __name__ == "__main__":
    main()
