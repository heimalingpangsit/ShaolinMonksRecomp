# Mods

The launcher reads the Krypt catalog when it opens.
New catalog entries appear in **Mods**. New compatible language packs also appear in the language selector.
An update notice appears when an installed mod has a newer compatible release.
Select **Update** to install it. Updates preserve the enabled state.
Choosing a local `.tar.xz` checks and installs the package directly.
Downloads and updates also install without another confirmation.

The installer checks downloads, files and patch results with SHA-256.
It prepares the complete installation before replacing the previous version.
Failed downloads, patches, conflicts or cancellations preserve that version.
The progress bar advances across the operation's stages. It reaches 100% after the operation succeeds.
Enabled mods apply when the game starts.

## Compatibility

- Different language packs can remain installed and enabled. Only the selected language applies.
- Two enabled packs cannot provide the same language.
- Content mods apply with the selected language pack.
- Mods cannot replace the same file, archive record, voice or embedded font.
- Replacing a whole archive conflicts with mods that replace its individual resources.
- Dependencies must be installed, enabled and sufficiently recent.
- An enabled dependent mod prevents disabling or removing its dependency.
- Routine replacements cannot target the same function. Before and after hooks can share a target.

The launcher reports conflicts when installing or enabling a mod.
It checks routine scripts again at game startup.
Content must match the supported original game and the declared minimum Recomp version.
Unknown future mod features remain unavailable until the runtime supports them.

## Prepare a package

Use the [Krypt builder](https://github.com/OAleex/ShaolinMonksRecompKrypt).
Add the mod to its `mods.json` before building.
The builder creates `mod.json` inside the package. Older packages with `manifest.json` remain supported.
Keep the mod ID and installation directory unchanged across updates.

Required metadata:

| Field | Purpose |
| --- | --- |
| `id` | Stable mod identifier |
| `name`, `summary`, `authors` | Information shown in Mods |
| `type` | `language`, `texture`, `data`, `gameplay` or `mixed` |
| `install_dir` | `packages/<id>` or `languages/<id>` for language packs |
| `versions` | Release records maintained by the builder |

Language mods declare their code in `language`.
Set `language_name` to show a readable name in the selector.
Custom languages use the original English slot. Prepare their text, fonts and audio for that slot.
Fonts retain the original resource headers and character encoding constraints.
New scripts do not add Unicode font rendering to the original game.

Put finished files in `mods/<id>/pack/`:

| Path | Runtime use |
| --- | --- |
| `gamedata/recordNNNNN.bin` | Replace an uncompressed PWF resource |
| `gamedata/recordNNNNN.ewdf` | Replace an EWDF resource after validation and decompression |
| `audio/voices/*.adx` | Replace matching recordings in the English and shared voice banks |
| `audio/movies/<name>.adx` and `.aix` | Replace both audio representations of a movie |
| `fonts/small.xpr`, `fonts/large.xpr` | Replace the embedded fonts |
| `files/<game path>` | Replace a directly read game file |
| `scripts/*.lua` | Supply routine callbacks listed in `routines` |
| `README.txt`, `CREDITS.md` | Instructions and attribution |

The archive planner combines compatible resources from active mods.
It preserves original disc files. Direct replacements apply only to game-disc reads.
They do not replace saves, settings or host files.
Both movie audio payloads must match their original track lengths.

Use `sources` to map replacement files to original game data for xdelta generation.
The builder infers sources for numbered `gamedata/` records.
Other source entries specify either `file`, optional `offset` and `size`, or `wad` and `record`.
All patches use the original game as their source, including updates.

`dependencies` contains objects with `id` and minimum `version`.
`conflicts` contains incompatible mod IDs.
`routines` contains the script paths to execute.
The builder derives required `features`, package hashes and release records.
New packages require Recomp 0.2.0 or later. Existing PT-BR packages remain supported.

## Routine scripts

Routine mods use Lua 5.4.9 source scripts.
Each script returns an array of hooks with a guest function `address` and callbacks.
Use `before` to change arguments or state before the original routine.
Use `after` to change its result or state afterward.
Use `replace` to supply the routine behavior.

```lua
return {
    {
        address = 0x00011BCC,
        before = function(ctx)
            local caller = game.read_u32(ctx.esp)
        end,
        after = function(ctx)
            -- Inspect or change guest registers and game state here.
        end,
    },
}
```

Determine targets and calling conventions from the supported game before writing a mod.
The generated `mod_routine_targets.h` lists available addresses for the current build.
Host compatibility overrides and functions without translated entries are unavailable.
Hooks cover direct and indirect calls through translated function entry points.
They do not intercept arbitrary instructions or internal jumps within a function.

Callbacks receive `eax`, `ecx`, `edx`, `ebx`, `esp`, `ebp`, `esi`, `edi` and the flags `cf`, `zf`, `sf`, `of`, `pf`, `df`.
Register values are unsigned 32-bit integers. Flags contain zero or one.
A replacement can declare `stack_pop`, in bytes, for callee-cleaned arguments.
The runtime also removes the four-byte return address after a successful replacement.
Do not remove it again in the script.

| API | Operation |
| --- | --- |
| `game.read_u8`, `game.read_u16`, `game.read_u32` | Read an unsigned value at a guest address |
| `game.write_u8`, `game.write_u16`, `game.write_u32` | Stage an unsigned value at a guest address |
| `game.read_f32`, `game.write_f32` | Read or stage a single-precision value |

Script reads see earlier staged writes from the same callback.
Writes commit only after the callback and register checks succeed.
A failed callback preserves its registers and staged memory changes.
The runtime logs the error and disables that script for the session.
A failed replacement falls back to the original routine.

Each script has an 8 MiB allocation limit and a budget of 100,000 Lua instructions per callback.
Active scripts share a 64 MiB allocation limit.
Callbacks can stage at most 256 memory writes.
Script entry files cannot exceed 1 MiB.
The runtime serializes callbacks within each script across guest threads.
Shared hooks follow mod-ID order and script-list order.

Host files, processes, networking, native modules and dynamic script loading are unavailable.
Protected calls, coroutines, metatable changes and native string-pattern searches are unavailable.
Guest memory access requires valid, committed addresses.
Changing guest code bytes does not change compiled instructions. Use routine hooks instead.
DLL, shared-library and raw Xbox executable mods are unsupported.

## Publish and update

1. Build the package with the Krypt builder.
2. Test the generated `.tar.xz` through **Install from file...**.
3. Attach it to a public Krypt release using the printed tag.
4. Publish the updated `mods.json` on the repository's default branch.

Publish the asset before the catalog that points to it.
Repeat this process with a higher version to offer an update.
Add a new mod entry before building a different mod.
The launcher reads the catalog rather than scanning release names.
New mods appear after reopening the launcher or selecting **Check for updates**.

Test appearance, audio and gameplay for each release.
Automated installer and routine tests cannot establish compatibility for every future mod.
