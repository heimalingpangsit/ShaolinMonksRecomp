"""Check a real translated entry hook with isolated files and a hidden game session."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='shaolin-mod-runtime-') as temp:
        root = Path(temp)
        pack = root / 'mods/packages/RoutineProbe'
        (pack / 'scripts').mkdir(parents=True)
        # A failed replacement must report its error and continue through the original entry.
        script = b'return {{address=0x000F40CA,replace=function(ctx) error("SHAOLIN_ROUTINE_PROBE") end}}'
        (pack / 'scripts/probe.lua').write_bytes(script)
        manifest = dict(schema=2, id='RoutineProbe', name='Routine probe', summary='Runtime regression',
                        authors=['Tester'], type='gameplay', install_dir='packages/RoutineProbe',
                        version='1.0.0', recomp='0.2.0', routines=['scripts/probe.lua'], features=['lua'],
                        game={'xbe_sha256': '48240902bf4a04eb89376217c529531032d3370749bcce91a194878cc49db74a'},
                        files=[dict(path='scripts/probe.lua', size=len(script), sha256=hashlib.sha256(script).hexdigest())], patches=[])
        encoded = json.dumps(json.dumps(manifest))
        (root / 'mods/installed.toml').write_text(
            'schema = 1\n[[mods]]\nid = "RoutineProbe"\nversion = "1.0.0"\nenabled = true\nmanifest = ' + encoded + '\n',
            encoding='utf-8')
        flags = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS if os.name == 'nt' else 0
        try:
            result = subprocess.run([str(args.exe.resolve()), '--background', '--timeout', '15',
                                     '--data-dir', str(root), '--save-dir', str(root / 'saves'), '--disc', str(args.disc.resolve())],
                                    cwd=root, capture_output=True, timeout=40, creationflags=flags)
        except subprocess.TimeoutExpired as error:
            result = subprocess.CompletedProcess(error.cmd, -1, error.stdout or b'', error.stderr or b'')
        logs = list((root / 'logs').glob('*.log'))
        text = '\n'.join(file.read_text(encoding='utf-8', errors='replace') for file in logs)
        text += result.stdout.decode('utf-8', errors='replace') + result.stderr.decode('utf-8', errors='replace')
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text, encoding='utf-8')
        assert result.returncode == 7, (result.returncode, text[-6000:])
        assert 'routine scripts active' in text, text[-6000:]
        assert 'SHAOLIN_ROUTINE_PROBE' in text and 'Script disabled for this session' in text, text[-6000:]
        assert 'Reset complete' in text, 'Original entry did not continue to device setup: ' + text[-6000:]
    print('PASS: installed Lua entry hook, controlled failure and original-game fallback')


if __name__ == '__main__':
    main()
