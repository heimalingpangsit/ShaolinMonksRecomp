"""Prepare real archives for the browser test. Read the owner's XBE without changing it."""
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile


def main():
    with tempfile.TemporaryDirectory(prefix='shaolin-browser-') as temp:
        root = Path(temp)
        (root / 'game').mkdir()
        shutil.copyfile(sys.argv[2], root / 'game/default.xbe')
        mods = []
        for mod_id, kind, language in [('Texture', 'texture', ''), ('Japanese', 'language', 'ja'), ('NewMod', 'data', '')]:
            mod = dict(id=mod_id, name=mod_id, summary='Browser fixture', authors=['Tester'],
                       type=kind, language=language, install_dir='packages/' + mod_id)
            versions = []
            for version in (['1.0.0', '2.0.0'] if mod_id in ('Texture', 'NewMod') else ['1.0.0']):
                payload = (mod_id + version).encode()
                manifest = dict(mod, schema=2, version=version, recomp='0.2.0',
                                game={'xbe_sha256': '48240902bf4a04eb89376217c529531032d3370749bcce91a194878cc49db74a'},
                                files=[dict(path='files/' + mod_id.lower() + '.bin', size=len(payload),
                                            sha256=hashlib.sha256(payload).hexdigest())], patches=[])
                archive = root / f'{mod_id}-{version}.tar.xz'
                with tarfile.open(archive, 'w:xz', format=tarfile.USTAR_FORMAT) as tar:
                    for name, data in {'mod.json': json.dumps(manifest).encode(), manifest['files'][0]['path']: payload}.items():
                        info = tarfile.TarInfo(name)
                        info.size = len(data)
                        tar.addfile(info, io.BytesIO(data))
                versions.insert(0, dict(version=version, recomp='0.2.0', size=archive.stat().st_size,
                                       sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                                       url='https://github.com/OAleex/ShaolinMonksRecompKrypt/releases/download/test/' + archive.name))
            mods.append(dict(mod, versions=versions))
        damaged = bytearray((root / 'NewMod-2.0.0.tar.xz').read_bytes())
        damaged[-20] ^= 1
        (root / 'bad-local.tar.xz').write_bytes(damaged)
        old = json.loads(json.dumps(mods[:2]))
        old[0]['versions'] = old[0]['versions'][1:]
        for number, catalog in [(1, old), (2, mods)]:
            game = {'xbe_sha256': '48240902bf4a04eb89376217c529531032d3370749bcce91a194878cc49db74a'}
            (root / f'catalog-{number}.json').write_text(json.dumps(dict(schema=2, game=game, mods=catalog)), encoding='utf-8')
        subprocess.run([sys.argv[1], str(root)], check=True)


if __name__ == '__main__':
    main()
