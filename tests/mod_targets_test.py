"""Check hook target generation against translated and host-owned functions."""
import importlib.util
from pathlib import Path
import tempfile
import unittest


class Targets(unittest.TestCase):
    def test_host_overrides_are_excluded(self):
        spec = importlib.util.spec_from_file_location('targets', Path(__file__).resolve().parents[1] / 'tools/prepare_mod_targets.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'src').mkdir()
            (root / 'adapted').mkdir()
            (root / 'mapping.cpp').write_text('const X86FuncMapping X86FuncMappings[] = {\n{0x100u, sub_00000100},\n{0x200u, sub_00000200},\n{0x300u, sub_00000300},\n{0, nullptr}};')
            (root / 'profile.toml').write_text('[symbols]\nXInputGetState = 0x200\n')
            (root / 'src/host.cpp').write_text('X86_FUNC(sub_00000300) { }')
            self.assertEqual(module.targets(root / 'mapping.cpp', root / 'profile.toml', [root / 'src', root / 'adapted']), [0x100])


if __name__ == '__main__':
    unittest.main()
