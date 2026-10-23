# Language packs

See [Mods](MODS.md) for other languages and package formats.
The preparation tools below produce LanguagePack PT-BR.

## Install LanguagePack PT-BR

1. Select **Brazilian Portuguese** as the game language.
2. Accept the prompt to install or enable **LanguagePack PT-BR**.
3. Wait until **Mods** shows the pack as enabled.

The language stays selected. You can select **Play** from **Mods**.

For a local `.tar.xz` package, use **Mods > Install from file...**.
The pack requires the original USA/Europe Xbox game.
Manage updates in **Mods**. Failed installations preserve the previous pack.
Play also offers installation if the selected language needs an inactive or missing pack.

Installed files are in `mods/languages/LanguagePack_PT-BR/` under the data folder.
Use **Files and support** to find that folder.

## Package format

The builder creates a `.tar.xz` containing mod files, xdelta patches and `mod.json`.
Installation checks the original game, package files and patch results with SHA-256 before replacing an installed pack.
Original game files remain unchanged.
Do not manually edit `mods/installed.toml`, which records installed versions and enabled states.

## Pack files

| Path | Content |
| --- | --- |
| `gamedata/recordNNNNN.ewdf` | Compressed resources identified by their original PWF record number |
| `gamedata/recordNNNNN.bin` | Uncompressed resources identified by their original PWF record number |
| `fonts/small.xpr`, `fonts/large.xpr` | Translated fonts |
| `audio/voices/*.adx` | Voice recordings with original filenames |
| `audio/movies/*.adx`, `audio/movies/*.aix` | Stereo ADX and six-channel AIX audio for four movies |
| `texts/game.json` | 1,751 main-text entries with stable IDs and original keys |
| `texts/system.json` | 12 system-text entries |
| `texts/credits.txt` | Credits source text |
| `README.txt` | Installation and editing instructions |
| `CREDITS.md` | Translation, voice cast and tool credits |

The runtime reads the binary text resources in `gamedata/`.
The files in `texts/` are editing sources.
Rebuild the binary resources after text edits.
Preserve IDs, keys, button markers, format placeholders and line breaks.
Preserve the original character codes.
The translated fonts use a/o umlaut codes for nasal glyphs.

The pack contains 1,015 voice recordings.
The runtime uses 988 matching filenames across the English and shared voice banks.
The other 27 recordings have no matching Xbox resources.

The six-channel movie audio uses the supplied stereo recordings. It is not a discrete surround mix.

## Runtime behavior

The runtime combines original data with installed replacements through WAD, AFS and SFD reads.
It applies fonts and resource tables in memory without rewriting game archives or creating a language cache.
The first read of each translated movie builds an index and can delay playback.

The pack uses the original English language slot.
Campaign ending credits finish naturally and cannot be skipped.
Extras credits still allow manual exit.

## Prepare the finished files

Use these tools:

- Python 3.11 or later.
- The supported original extracted game and the private translation sources.
- An executable compiled from the supplied `ewdf_miniz_cli.cpp`, with miniz.
- FFmpeg, FFprobe and VGAudioCli for the movie audio tool in the translation sources.

Define `MZ_DEFAULT_MEMLEVEL=9` when compiling the encoder with this project's pinned miniz.
This miniz revision does not declare that macro.

The preparer checks both XBE hashes and verifies each encoded EWDF against its source.
It extracts fonts without including either XBE in the pack.
It preserves movie sample counts, audio headers and payload sizes.
See [Building](BUILDING.md) for the supported original hash.

```powershell
python tools/prepare_language_pack.py --source "C:/TranslationSources/PT-BR" --game "C:/Games/ShaolinMonks" --output ".private/LanguagePack_PT-BR" --ewdf-encoder "C:/TranslationTools/ewdf_miniz_cli.exe" --report "work/language-pack/verification.json"
```

Select a new output directory outside both input directories.
Keep the report outside the pack and input directories.
The preparer preserves the inputs and refuses to replace an existing pack.
It writes the pack only after all checks pass.

## Build the release package

1. Copy the finished files into `mods/LanguagePack_PT-BR/pack/` in the Krypt repository.
2. Run its `build.bat`.
3. Enter the requested game folder and mod version.
4. Test the generated `.tar.xz` through **Mods > Install from file...**.

The release builder converts replacement files into xdelta patches.
It adds `mod.json` and writes the package to `dist/`.
See the [Krypt instructions](https://github.com/OAleex/ShaolinMonksRecompKrypt) for catalog and publication details.

## Checks

The `--language-pack` option selects finished files directly for authoring checks.
It does not register an installation in the Mods tab.

```powershell
python tests/language_pack_test.py
ShaolinMonksRecomp.exe --validate-language-pack --game "C:/Games/ShaolinMonks" --language-pack ".private/LanguagePack_PT-BR" --data-dir "work/language-validation/data"
```

Use `./ShaolinMonksRecomp` when the executable has no `.exe` extension.
The diagnostic checks all four movie indexes before it exits.
It does not start gameplay, audio or controller input.

Visual appearance, audio quality and full campaign playback require manual checks.

To compare direct reads with files from a previously checked build:

```powershell
build/release/recomp_language_pack_test.exe "C:/Games/ShaolinMonks" ".private/LanguagePack_PT-BR" "C:/LanguageReference"
```

This test compares eight virtual WAD files and four virtual SFD files without exporting them.

## Distribution

Keep mod packages separate from the base release.
Base packages exclude translation assets and authoring tools.
Include `CREDITS.md` with the mod.
See [translation credits](TRANSLATION_CREDITS.md) for the cast and contributors.
The project code license does not grant rights to language-pack assets.
