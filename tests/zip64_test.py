"""Check extended ZIP offsets without large fixtures or original game assets."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile
from recomp_game_fixture import write_zip64_offsets


def main():
    with tempfile.TemporaryDirectory(prefix='shaolin-zip64-') as temporary:
        root = Path(temporary)
        files = {'one.txt': b'first', 'two.txt': b'second'}
        write_zip64_offsets(root / 'valid.zip', files)
        write_zip64_offsets(root / 'out-of-bounds.zip', files, invalid_offset=True)
        data = bytearray((root / 'valid.zip').read_bytes())
        with zipfile.ZipFile(root / 'valid.zip') as archive:
            offset = archive.getinfo('game/two.txt').header_offset
        name, extra = struct.unpack_from('<HH', data, offset + 26)
        data[offset + 30 + name + extra] ^= 1
        (root / 'corrupt.zip').write_bytes(data)
        subprocess.run([sys.argv[1], str(root)], check=True)


if __name__ == '__main__':
    main()
