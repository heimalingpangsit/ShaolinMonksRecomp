"""Create synthetic USTAR/XZ releases and exercise the native installer."""
import hashlib
import io
import json
import lzma
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

GAME = '48240902bf4a04eb89376217c529531032d3370749bcce91a194878cc49db74a'
PATCH = bytes.fromhex('d6c3c40004177461726765742e62696e2f2f736f757263652e62696e2f051c00208874000b080326ae79cc7472616e736c6174656421140b201387720064000c20')


def archive(path, manifest, extra=None, metadata='mod.json'):
    with tarfile.open(path, 'w:xz', format=tarfile.USTAR_FORMAT, preset=9) as tar:
        files = {'notes/new.txt': b'new content', 'fonts/test.xpr.xdelta': PATCH,
                 metadata: (json.dumps(manifest, indent=2) + '\n').encode()}
        files.update(extra or {})
        for name, data in files.items():
            entry = tarfile.TarInfo(name)
            entry.size = len(data)
            tar.addfile(entry, io.BytesIO(data))


def main():
    with tempfile.TemporaryDirectory(prefix='shaolin-mod-test-') as temp:
        root = Path(temp)
        target = b'The translated game data.\n' * 40 + b'!' * 100
        manifest = dict(schema=1, id='LanguagePack_PT-BR', name='Test LanguagePack', summary='Synthetic test',
                        authors=['Tester'], type='language', language='pt-BR', install_dir='languages/LanguagePack_PT-BR',
                        version='1.0.0', recomp='0.1.0', game={'xbe_sha256': GAME},
                        files=[dict(path='notes/new.txt', size=11, sha256=hashlib.sha256(b'new content').hexdigest())],
                        patches=[dict(path='fonts/test.xpr', source={'file': 'default.xbe', 'offset': 0, 'size': 920},
                                      source_sha256=hashlib.sha256(b'The original game data.\n' * 40).hexdigest(),
                                      target_sha256=hashlib.sha256(target).hexdigest())])
        archive(root / 'good.tar.xz', manifest)
        archive(root / 'legacy.tar.xz', manifest, metadata='manifest.json')
        archive(root / 'both-metadata.tar.xz', manifest, {'manifest.json': json.dumps(manifest).encode()})
        archive(root / 'large-metadata.tar.xz', manifest, {'mod.json': b' ' * (8 * 1024 * 1024 + 1)})
        for mod_id, kind, language, target, dependencies in [
                ('TextureA', 'texture', '', 'files/shared.bin', []),
                ('TextureB', 'texture', '', 'files/shared.bin', []),
                ('Accessory', 'mixed', '', 'files/accessory.bin', [{'id': 'TextureA', 'version': '1.0.0'}]),
                ('Japanese', 'language', 'ja', 'files/japanese.bin', []),
                ('OtherJapanese', 'language', 'ja', 'files/other-japanese.bin', []),
                ('Korean', 'language', 'ko', 'files/japanese.bin', [])]:
            extra = dict(manifest, schema=2, id=mod_id, type=kind, language=language,
                         install_dir='packages/' + mod_id, dependencies=dependencies, patches=[],
                         files=[dict(path=target, size=3, sha256=hashlib.sha256(b'abc').hexdigest())])
            with tarfile.open(root / (mod_id + '.tar.xz'), 'w:xz', format=tarfile.USTAR_FORMAT) as tar:
                for name, data in {'mod.json': json.dumps(extra).encode(), target: b'abc'}.items():
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    tar.addfile(info, io.BytesIO(data))
        manifest['version'] = '2.0.0'
        manifest['patches'][0]['target_sha256'] = '0' * 64
        archive(root / 'bad-hash.tar.xz', manifest)
        archive(root / 'traversal.tar.xz', manifest, {'../escape': b'escape'})
        archive(root / 'extra.tar.xz', manifest, {'undeclared': b'bad'})
        data = bytearray((root / 'good.tar.xz').read_bytes())
        data[-20] ^= 1
        (root / 'bad-xz.tar.xz').write_bytes(data)
        # A closed XZ stream can still contain a TAR interrupted before its terminator.
        raw = lzma.decompress((root / 'good.tar.xz').read_bytes())
        with tarfile.open(root / 'good.tar.xz') as tar:
            last = tar.getmembers()[-1]
            end = last.offset_data + (last.size + 511) // 512 * 512
        (root / 'unfinished-tar.tar.xz').write_bytes(lzma.compress(raw[:end]))
        subprocess.run([sys.argv[1], str(root)], check=True)


if __name__ == '__main__':
    main()
