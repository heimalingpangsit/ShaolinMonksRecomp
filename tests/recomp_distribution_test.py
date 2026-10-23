"""Check installation paths, settings migration, and rejected disc selection."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time


@contextmanager
def temporary_installation():
    directory = tempfile.TemporaryDirectory(prefix='shaolin-distribution-')
    try:
        yield directory.name
    finally:
        # Windows scanners can retain a new executable briefly after its process exits.
        for attempt in range(50):
            try:
                directory.cleanup()
                break
            except PermissionError:
                if attempt == 49:
                    raise
                time.sleep(.1)


def check_icon(exe):
    image = exe.read_bytes()
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    optional = pe + 24
    section_count = struct.unpack_from('<H', image, pe + 6)[0]
    optional_size = struct.unpack_from('<H', image, pe + 20)[0]
    resource_rva = struct.unpack_from('<I', image, optional + 112 + 2 * 8)[0]
    assert resource_rva, 'The executable has no Windows resources'
    sections = []
    for index in range(section_count):
        at = optional + optional_size + index * 40
        size, rva, raw_size, offset = struct.unpack_from('<IIII', image, at + 8)
        sections.append((rva, max(size, raw_size), offset))

    def file_offset(rva):
        for start, size, offset in sections:
            if start <= rva < start + size:
                return offset + rva - start
        raise AssertionError('Resource data is outside the PE sections')

    base = file_offset(resource_rva)

    def entries(relative):
        at = base + relative
        named, ids = struct.unpack_from('<HH', image, at + 12)
        return [struct.unpack_from('<II', image, at + 16 + index * 8) for index in range(named + ids)]

    types = dict(entries(0))
    assert 14 in types, 'The executable must contain an icon group'
    names = dict(entries(types[14] & 0x7fffffff))
    assert 1 in names, 'The window application icon is missing'
    languages = entries(names[1] & 0x7fffffff)
    data = base + (languages[0][1] & 0x7fffffff)
    group_rva = struct.unpack_from('<I', image, data)[0]
    reserved, kind, count = struct.unpack_from('<HHH', image, file_offset(group_rva))
    assert reserved == 0 and kind == 1 and count > 0, 'The icon group is invalid'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    args = parser.parse_args()
    with temporary_installation() as name:
        root = Path(name)
        install = root / 'install'
        install.mkdir()
        exe = install / args.exe.name
        shutil.copy2(args.exe.resolve(), exe)
        data = root / 'User data with spaces \u00e7'
        legacy = install / 'shaolin-recomp.toml'
        legacy.write_text('fps_cap = 60\n[controller]\ndeadzone = 0.23\n')

        def run(*arguments, expected=0):
            result = subprocess.run([str(exe), *map(str, arguments)],
                                    capture_output=True, text=True, encoding='utf-8', timeout=20,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            assert result.returncode == expected, (arguments, result.returncode, result.stdout, result.stderr[-3000:])
            return result

        paths = json.loads(run('--data-dir', data, '--paths').stdout)
        assert not data.exists(), 'Path inspection must not create or migrate user data'
        run('--data-dir', data, '--validate-disc', root / 'missing.iso', expected=3)
        assert Path(paths['data']) == data
        assert Path(paths['config']) == data / 'settings.toml'
        assert Path(paths['logs']) == data / 'logs'
        assert Path(paths['cache']) == data / 'cache'
        assert Path(paths['crashes']) == data / 'crashes'
        settings = Path(paths['config'])
        assert settings.read_bytes() == legacy.read_bytes(), 'Existing controller settings must migrate without changes'
        settings.write_text('fps_cap = 30\n')
        run('--data-dir', data, '--validate-disc', root / 'missing.iso', expected=3)
        assert settings.read_text() == 'fps_cap = 30\n', 'Migration must preserve the destination settings'
        assert legacy.exists(), 'Migration must preserve its source'
        isolated = json.loads(run('--background', '--paths').stdout)
        assert Path(isolated['data']) == install / 'runtime-data'
        default = json.loads(run('--paths').stdout)
        assert Path(default['data']).name == 'Shaolin Monks Recomp'
        assert 'appdata' in str(Path(default['data'])).lower() or Path(default['data']).parent.exists()
        run('--background', '--setup', expected=2)
        run('--data-dir', data, '--register-disc', root / 'missing.xiso.iso', expected=3)
        assert not (data / 'game-source.toml').exists()
        invalid = root / 'invalid.xiso.iso'
        invalid.write_bytes(b'This is not an Xbox disc image.')
        rejected = run('--data-dir', data, '--register-disc', invalid, expected=3)
        assert 'Xbox disc image' in rejected.stderr
        assert not (data / 'game-source.toml').exists()
        check_icon(exe)
        print('PASS: AppData paths, isolated diagnostics, settings migration, disc rejection, and application icon')


if __name__ == '__main__':
    main()
