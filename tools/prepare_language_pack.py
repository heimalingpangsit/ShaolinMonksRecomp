"""Prepare convention-based language resources without modifying the source assets."""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib


ORIGINAL_XBE_SHA256 = "48240902bf4a04eb89376217c529531032d3370749bcce91a194878cc49db74a"
TRANSLATED_XBE_SHA256 = "9b02e038e17ce02d5ca9f0e421e3f97e59e17fb7810848bd6262f46727815f1d"
FONT_REGIONS = (("small.xpr", 0x51D3E8, 0x4800), ("large.xpr", 0x532018, 0x20800))
MOVIES = ("opening", "end006", "end011", "final")
PACK_README = """LanguagePack PT-BR

Brazilian Portuguese text, subtitles, voices and fonts for Shaolin Monks Recomp.

Installation

1. Select Brazilian Portuguese as the game language in the launcher.
2. Accept the prompt to install or enable LanguagePack PT-BR.
3. Wait until Mods shows the pack as enabled.

The language stays selected. You can select Play from Mods.

For a downloaded .tar.xz package, use Mods > Install from file...
This pack requires the original USA/Europe Xbox game.
Manage updates in Mods.
The original game files remain unchanged.

Text editing

Preserve IDs, keys, button markers, format placeholders and character codes.
Rebuild the binary resources in gamedata after editing texts.
The game does not read the JSON source files.

The six-channel movie audio uses stereo recordings.
Translation, voice cast and tool credits: CREDITS.md.
"""


def decode_text_table(data: bytes) -> list[dict]:
    if len(data) < 4:
        raise ValueError("The text table has no count")
    count = struct.unpack_from("<I", data)[0]
    if count > (len(data) - 4) // 9:
        raise ValueError("The text table count exceeds its data")
    entries, cursor = [], 4
    for index in range(count):
        if cursor + 8 > len(data):
            raise ValueError(f"Truncated text entry {index}")
        key, length = struct.unpack_from("<II", data, cursor)
        cursor += 8
        if not length or cursor + length > len(data):
            raise ValueError(f"Invalid text length at entry {index}")
        value = data[cursor:cursor + length]
        if value[-1] != 0 or b"\0" in value[:-1]:
            raise ValueError(f"Invalid text terminator at entry {index}")
        entries.append({"id": index, "key": f"{key:08x}", "text": value[:-1].decode("cp1252")})
        cursor += length
    if cursor != len(data):
        raise ValueError("Unexpected trailing text-table data")
    return entries


def encode_text_table(entries: list[dict]) -> bytes:
    result = bytearray(struct.pack("<I", len(entries)))
    for index, entry in enumerate(entries):
        if entry["id"] != index:
            raise ValueError("Text IDs must remain contiguous and in their original order")
        try:
            text = entry["text"].encode("cp1252")
            key = int(entry["key"], 16)
        except (UnicodeError, TypeError, ValueError) as error:
            raise ValueError(f"Text entry {index} cannot be represented by the guest font") from error
        if b"\0" in text or not 0 <= key <= 0xFFFFFFFF:
            raise ValueError(f"Invalid text entry {index}")
        result.extend(struct.pack("<II", key, len(text) + 1))
        result.extend(text + b"\0")
    return bytes(result)


def validate_output(source: Path, game: Path, output: Path) -> None:
    source, game, output = source.resolve(), game.resolve(), output.resolve()
    for original in (source, game):
        if output == original or output.is_relative_to(original) or original.is_relative_to(output):
            raise ValueError("The output must be separate from the translation source and original game")
    if output.exists():
        raise ValueError("The output already exists; choose a new directory to preserve the previous pack")


def copy_resources(source: Path, output: Path) -> list[dict]:
    selected = []
    for path in sorted((source / "GAMEDATAS").glob("*")):
        if path.is_file() and path.suffix.lower() == ".adx":
            selected.append((path, Path("audio/voices") / path.name.lower()))
    for path in sorted((source / "Graficos E TEXTURAS").rglob("*")):
        if path.is_file() and path.suffix.lower() in {".wad", ".p2n"}:
            selected.append((path, Path("resources") / path.name.lower()))
    names = [str(relative).lower() for _, relative in selected]
    if len(names) != len(set(names)):
        raise ValueError("Two source resources resolve to the same package filename")
    records = []
    for path, relative in selected:
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        records.append({"path": relative.as_posix(), "size": target.stat().st_size,
                        "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
    return records


def extract_xpr(data: bytes, offset: int, size: int) -> bytes:
    resource = data[offset:offset + size]
    if len(resource) != size or size < 12 or resource[:4] != b"XPR0":
        raise ValueError("The embedded font is not a complete XPR0 resource")
    total, header = struct.unpack_from("<II", resource, 4)
    if total != size or not 12 <= header <= total:
        raise ValueError("The embedded font has invalid XPR0 bounds")
    return resource


def movie_track_ids(tracks: list[dict]) -> tuple[int, int]:
    selected = {track["id"]: track for track in tracks}
    for stream, codec, channels in ((0xC0, "adpcm_adx", 2), (0xC5, "aix", 6)):
        if stream not in selected or selected[stream]["codec"] != codec or selected[stream]["channels"] != channels:
            raise ValueError("The movie must have its expected stereo ADX and six-channel AIX templates")
    return 0xC0, 0xC5


def decode_pwf_record(first: int, second: int) -> tuple[int, int, bool]:
    if second & 0x80000000:
        return (first & 0x1FFFFF) * 0x800, ((first >> 22) | (second << 10)) & 0x3FFFFF, True
    return first, second, False


def discover_resource_targets(game: Path, names: list[str]) -> dict[str, dict]:
    with (game / "GAMEDATA.WAD").open("rb") as file:
        header = file.read(32)
        if len(header) != 32 or header[:4] != b"PWF ":
            raise ValueError("The original GAMEDATA.WAD is not PWF")
        count, stride = struct.unpack_from("<II", header, 12)
        if not 0 < count <= 100000 or not stride:
            raise ValueError("Invalid PWF directory bounds")
        table = file.read(count * 8)
    if len(table) != count * 8:
        raise ValueError("Truncated PWF directory")
    wanted = {}
    for name in names:
        if re.match(r"^[0-9a-fA-F]{8}_", name):
            wanted.setdefault(int(name[:8], 16), []).append(name)
    candidates = {name: [] for name in names}
    with ExitStack() as stack:
        files = {}
        for record in range(count):
            offset, size, packed = decode_pwf_record(*struct.unpack_from("<II", table, record * 8))
            if not size:
                continue
            volume, within = divmod(offset, stride)
            if volume > 22:
                raise ValueError("Invalid PWF volume index")
            volume_name = f"GAMEDATA.WA{chr(ord('D') + volume)}"
            if volume_name not in files:
                files[volume_name] = stack.enter_context((game / volume_name).open("rb"))
            file = files[volume_name]
            file.seek(0, 2)
            if within + size > file.tell():
                raise ValueError(f"PWF record {record} exceeds its original volume")
            file.seek(within)
            prefix = file.read(min(size, 512))
            item = {"record": record, "packed": packed, "original_stored_size": size}
            if prefix[:4] == b"EWDF" and len(prefix) >= 17:
                length, payload, stamp = struct.unpack_from("<III", prefix, 4)
                if not 17 <= payload <= len(prefix) or prefix[payload - 1] != 0:
                    raise ValueError(f"Invalid EWDF header in record {record}")
                item.update(internal_name=prefix[16:payload - 1].decode("ascii"),
                            header_hex=prefix[:payload].hex(), original_decoded_size=length)
                for name in wanted.get(stamp, []):
                    candidates[name].append(item)
            if record == 158 and "xbox_system_english.bin" in candidates:
                if prefix[:4] != struct.pack("<I", 12) or b"UNDEFINED STRING\0" not in prefix:
                    raise ValueError("The original system-text resource does not match the supported profile")
                candidates["xbox_system_english.bin"].append(item)
            for name in names:
                match = re.match(r"^record(\d+)_", name, re.IGNORECASE)
                if match and int(match[1]) == record:
                    candidates[name].append(item)
    result = {}
    for name, matches in candidates.items():
        if len(matches) != 1:
            raise ValueError(f"Resource {name} has {len(matches)} original matches; exactly one is required")
        result[name] = matches[0]
    return result


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        while chunk := file.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def encode_resource(encoder: Path, source: Path, output: Path, target: dict) -> None:
    if not target["packed"]:
        shutil.copyfile(source, output)
        return
    header = bytes.fromhex(target["header_hex"])
    completed = subprocess.run(
        [str(encoder), "encode", str(source), str(output), target["internal_name"], header[12:16].hex(), "10"],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if completed.returncode:
        raise ValueError("EWDF encoding failed: " + (completed.stderr.strip() or completed.stdout.strip()))
    data = output.read_bytes()
    if data[:4] != b"EWDF" or data[12:len(header)] != header[12:]:
        raise ValueError("The encoder changed the EWDF resource identity")
    length, start = struct.unpack_from("<II", data, 4)
    payload = source.read_bytes()
    stream = zlib.decompressobj(-15)
    decoded = stream.decompress(data[start:]) + stream.flush()
    if not stream.eof or length != len(payload) or decoded != payload:
        raise ValueError("The encoded EWDF did not round-trip to the exact translated resource")
    trailer = stream.unused_data
    if len(trailer) < 8 or struct.unpack_from("<II", trailer) != (zlib.crc32(payload), len(payload)):
        raise ValueError("The encoded EWDF has an invalid checksum or size trailer")


def prepare_movies(source: Path, game: Path, output: Path) -> list[dict]:
    tool = source / "Textos/videos sfd dublado/sfd_audio_core.py"
    spec = importlib.util.spec_from_file_location("language_pack_sfd_audio", tool)
    core = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = core
    spec.loader.exec_module(core)
    records = []
    for name in MOVIES:
        print(f"Preparing movie audio: {name} (stereo ADX and six-channel AIX)", flush=True)
        original = game / "front/movies" / f"{name}.sfd"
        wave = source / "Textos/videos sfd dublado" / f"{name}.wav"
        analysis = core.analyze_sfd(original)
        stereo_id, surround_id = movie_track_ids([
            {"id": track.stream_id, "codec": track.codec_name, "channels": track.channels}
            for track in analysis.tracks])
        stereo, surround = analysis.track_by_stream_id(stereo_id), analysis.track_by_stream_id(surround_id)
        original_adx = core.read_stream_payload(original, analysis.spans_by_stream[stereo_id])
        original_aix = core.read_stream_payload(original, analysis.spans_by_stream[surround_id])
        with tempfile.TemporaryDirectory(prefix="movie-audio-", dir=output.parent) as temporary:
            working = Path(temporary)
            encoded = core._encode_wave_to_adx(wave, stereo.adx, working / "stereo.adx", None)
            adx = core._build_grafted_adx(original_adx, encoded)
            aix = core._encode_wave_to_aix(wave, original_aix, surround.aix, working, None)
        if core.parse_adx_header(adx) != stereo.adx or core.parse_aix_payload(aix) != surround.aix:
            raise ValueError(f"Movie audio changed its structural parameters: {name}")
        folder = output / "audio/movies"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / f"{name}.adx").write_bytes(adx)
        (folder / f"{name}.aix").write_bytes(aix)
        records.append({"movie": name, "source_sfd_sha256": sha256_file(original),
                        "source_wave_sha256": sha256_file(wave), "sample_rate": stereo.sample_rate,
                        "sample_count": stereo.adx.sample_count,
                        "stereo_bytes": len(adx), "surround_bytes": len(aix),
                        "source_channels": core.inspect_wave(wave).channels})
    return records


def build_pack(source: Path, game: Path, output: Path, encoder: Path, report: Path) -> None:
    source, game, output, encoder, report = [p.resolve() for p in (source, game, output, encoder, report)]
    validate_output(source, game, output)
    if report.is_relative_to(output) or report.is_relative_to(source) or report.is_relative_to(game):
        raise ValueError("The generated build report must stay outside the pack and input directories")
    if sha256_file(game / "default.xbe") != ORIGINAL_XBE_SHA256:
        raise ValueError("The original executable does not match the supported USA/Europe profile")
    translated_xbe = source / "default.xbe"
    if sha256_file(translated_xbe) != TRANSLATED_XBE_SHA256:
        raise ValueError("The supplied no-debug translation executable differs from the inspected profile")
    if not encoder.is_file():
        raise ValueError("The EWDF encoder is missing")
    output.parent.mkdir(parents=True, exist_ok=True)
    report.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{output.name}-build-", dir=output.parent) as directory:
        stage = Path(directory) / output.name
        copied = copy_resources(source, stage)
        tables = [("game", source / "Textos/b3f9f220_h/b3f9f220_h.bin"),
                  ("system", source / "Textos/xbox_system_english/xbox_system_english.bin")]
        text_counts = {}
        (stage / "texts").mkdir()
        for name, path in tables:
            raw = path.read_bytes()
            entries = decode_text_table(raw)
            if encode_text_table(entries) != raw:
                raise ValueError(f"The {name} text table did not round-trip")
            (stage / "texts" / f"{name}.json").write_text(
                json.dumps(entries, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            text_counts[name] = len(entries)
        credits = source / "Textos/1af62f16_SDATA_XBX.TXT"
        shutil.copyfile(credits, stage / "texts/credits.txt")
        # Numeric names bind resources to the audited disc directory without a manual manifest.
        resources = list((stage / "resources").iterdir()) + [path for _, path in tables] + [credits]
        names = [path.name for path in resources]
        targets = discover_resource_targets(game, names)
        (stage / "gamedata").mkdir()
        resource_report = []
        for path in resources:
            target = targets[path.name]
            extension = ".ewdf" if target["packed"] else ".bin"
            relative = Path("gamedata") / f"record{target['record']:05d}{extension}"
            if (stage / relative).exists():
                raise ValueError("Two translated resources target the same original record")
            encode_resource(encoder, path, stage / relative, target)
            resource_report.append({"source_name": path.name, "path": relative.as_posix(), **target})
        # These are generated staging copies, never the author's resource files.
        for path in (stage / "resources").iterdir():
            path.unlink()
        (stage / "resources").rmdir()
        original_xbe = (game / "default.xbe").read_bytes()
        translated = translated_xbe.read_bytes()
        (stage / "fonts").mkdir()
        for name, offset, size in FONT_REGIONS:
            before, after = extract_xpr(original_xbe, offset, size), extract_xpr(translated, offset, size)
            header_size = struct.unpack_from("<I", before, 8)[0]
            if before[:header_size] != after[:header_size]:
                raise ValueError("The translated font changes the original XPR0 header")
            (stage / "fonts" / name).write_bytes(after)
        movies = prepare_movies(source, game, stage)
        shutil.copyfile(Path(__file__).resolve().parents[1] / "docs/TRANSLATION_CREDITS.md", stage / "CREDITS.md")
        (stage / "README.txt").write_text(PACK_README, encoding="utf-8")
        inventory = [{"path": p.relative_to(stage).as_posix(), "size": p.stat().st_size,
                      "sha256": sha256_file(p)} for p in sorted(stage.rglob("*")) if p.is_file()]
        result = {"format": "shaolin-loose-language-resources-v1", "language": "pt-BR",
                  "runtime_integration": "native_direct_pack", "original_xbe_sha256": ORIGINAL_XBE_SHA256,
                  "translation_xbe_sha256": TRANSLATED_XBE_SHA256,
                  "voices": sum(row["path"].startswith("audio/voices/") for row in copied),
                  "text_entries": text_counts, "resources": resource_report, "movies": movies,
                  "files": inventory, "total_bytes": sum(row["size"] for row in inventory)}
        os.rename(stage, output)
        report.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Prepared {len(inventory)} files ({result['total_bytes'] / 1024**2:.2f} MiB): {output}", flush=True)
    print(f"Build verification report: {report}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="Private translation source directory")
    parser.add_argument("--game", required=True, type=Path, help="Original extracted Xbox game directory, read only")
    parser.add_argument("--output", required=True, type=Path, help="New language-pack directory")
    parser.add_argument("--ewdf-encoder", required=True, type=Path, help="Compiled author-supplied ewdf_miniz_cli encoder")
    parser.add_argument("--report", required=True, type=Path, help="Generated verification report outside the pack")
    args = parser.parse_args()
    try:
        build_pack(args.source, args.game, args.output, args.ewdf_encoder, args.report)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Language-pack preparation failed: {error}\n")


if __name__ == "__main__":
    main()
