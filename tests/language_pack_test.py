"""Exercise language-pack preparation with synthetic assets, never game files."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / "tools" / "prepare_language_pack.py"


class LanguagePackTests(unittest.TestCase):
    def module(self):
        self.assertTrue(TOOL.is_file(), "The language-pack preparer is not implemented")
        spec = importlib.util.spec_from_file_location("prepare_language_pack", TOOL)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module

    def test_text_export_preserves_ids_empty_lines_and_guest_glyphs(self):
        module = self.module()
        strings = [(0, b""), (0x12345678, b"N\xe4o\n%d ="), (0, b"Last")]
        data = struct.pack("<I", 3)
        for key, text in strings:
            data += struct.pack("<II", key, len(text) + 1) + text + b"\0"
        entries = module.decode_text_table(data)
        self.assertEqual(entries, [
            {"id": 0, "key": "00000000", "text": ""},
            {"id": 1, "key": "12345678", "text": "N\u00e4o\n%d ="},
            {"id": 2, "key": "00000000", "text": "Last"},
        ])
        self.assertEqual(module.encode_text_table(entries), data)

    def test_text_table_rejects_truncation_and_unrepresentable_edits(self):
        module = self.module()
        for data in [b"", struct.pack("<III", 1, 0, 9) + b"short", struct.pack("<III", 1, 0, 2) + b"ab"]:
            with self.subTest(data=data), self.assertRaises(ValueError):
                module.decode_text_table(data)
        with self.assertRaises(ValueError):
            module.encode_text_table([{"id": 0, "key": "00000000", "text": "\u65e5"}])

    def test_copy_stage_excludes_authoring_files_and_debug_binaries(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "source", root / "pack"
            (source / "GAMEDATAS").mkdir(parents=True)
            (source / "GAMEDATAS" / "Voice_A.adx").write_bytes(b"new voice")
            graphic = source / "Graficos E TEXTURAS" / "12345678_menu"
            graphic.mkdir(parents=True)
            (graphic / "12345678_menu.P2N").write_bytes(b"new texture")
            (graphic / "author.psd").write_bytes(b"do not package")
            (graphic / "preview.png").write_bytes(b"do not package")
            (source / "debug_default.xbe").write_bytes(b"do not package")
            records = module.copy_resources(source, output)
            files = sorted(str(p.relative_to(output)).replace("\\", "/") for p in output.rglob("*") if p.is_file())
            self.assertEqual(files, ["audio/voices/voice_a.adx", "resources/12345678_menu.p2n"])
            self.assertEqual((output / "audio/voices/voice_a.adx").read_bytes(), b"new voice")
            self.assertEqual(len(records), 2)
            self.assertEqual((source / "GAMEDATAS/Voice_A.adx").read_bytes(), b"new voice")

    def test_resource_name_collision_is_rejected(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for part in ("first", "second"):
                path = root / "source/Graficos E TEXTURAS" / part
                path.mkdir(parents=True)
                (path / "12345678_same.P2N").write_bytes(part.encode())
            with self.assertRaises(ValueError):
                module.copy_resources(root / "source", root / "pack")

    def test_output_cannot_overlap_inputs_or_replace_an_existing_pack(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, game = root / "source", root / "game"
            source.mkdir(); game.mkdir()
            for output in (source, source / "nested", root, game, game / "nested"):
                with self.subTest(output=output), self.assertRaises(ValueError):
                    module.validate_output(source, game, output)
            existing = root / "existing"
            existing.mkdir()
            (existing / "keep").write_bytes(b"preserve")
            with self.assertRaises(ValueError):
                module.validate_output(source, game, existing)
            self.assertEqual((existing / "keep").read_bytes(), b"preserve")

    def test_font_extraction_rejects_an_invalid_xpr_header(self):
        module = self.module()
        self.assertTrue(hasattr(module, "extract_xpr"), "Font extraction is not implemented")
        data = b"padding!" + struct.pack("<4sII", b"XPR0", 32, 16) + b"\0" * 20
        self.assertEqual(module.extract_xpr(data, 8, 32), data[8:])
        for offset, size in [(8, 31), (9, 32), (8, 33)]:
            with self.subTest(offset=offset, size=size), self.assertRaises(ValueError):
                module.extract_xpr(data, offset, size)

    def test_movie_selection_requires_both_expected_audio_representations(self):
        module = self.module()
        self.assertTrue(hasattr(module, "movie_track_ids"), "Dual-format movie selection is not implemented")
        tracks = [{"id": 0xC0, "codec": "adpcm_adx", "channels": 2},
                  {"id": 0xC5, "codec": "aix", "channels": 6},
                  {"id": 0xC1, "codec": "adpcm_adx", "channels": 2}]
        self.assertEqual(module.movie_track_ids(tracks), (0xC0, 0xC5))
        with self.assertRaises(ValueError):
            module.movie_track_ids(tracks[:1])
        tracks[1]["channels"] = 2
        with self.assertRaises(ValueError):
            module.movie_track_ids(tracks)

    def test_pwf_directory_decodes_both_observed_record_encodings(self):
        module = self.module()
        self.assertTrue(hasattr(module, "decode_pwf_record"), "PWF record decoding is not implemented")
        self.assertEqual(module.decode_pwf_record(0x116800, 844), (0x116800, 844, False))
        self.assertEqual(module.decode_pwf_record(0x34000123, 0x80000112), (0x91800, 0x448D0, True))
        self.assertEqual(module.decode_pwf_record(0x7980021A, 0x81FFA011), (1101824, 17894, True))

    def test_resource_mapping_uses_ewdf_identity_and_rejects_ambiguity(self):
        module = self.module()
        self.assertTrue(hasattr(module, "discover_resource_targets"), "Resource discovery is not implemented")
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            ewdf = struct.pack("<4sIII", b"EWDF", 4, 21, 0x12345678) + b"menu\0" + b"data"
            wad = bytearray(0x800) + ewdf
            struct.pack_into("<4s5I", wad, 0, b"PWF ", len(wad), 2, 1, 0x12C00000, 0x800)
            struct.pack_into("<II", wad, 32, 0x800, len(ewdf))
            (game / "GAMEDATA.WAD").write_bytes(wad)
            mapping = module.discover_resource_targets(game, ["12345678_menu.P2N"])
            self.assertEqual(mapping["12345678_menu.P2N"]["record"], 0)
            self.assertEqual(mapping["12345678_menu.P2N"]["internal_name"], "menu")
            self.assertEqual(mapping["12345678_menu.P2N"]["header_hex"], ewdf[:21].hex())
            struct.pack_into("<I", wad, 12, 2)
            struct.pack_into("<II", wad, 40, 0x800, len(ewdf))
            (game / "GAMEDATA.WAD").write_bytes(wad)
            with self.assertRaises(ValueError):
                module.discover_resource_targets(game, ["12345678_menu.P2N"])


if __name__ == "__main__":
    unittest.main()
