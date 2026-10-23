# Troubleshooting

## Game selection

Use **Files and support > Choose...** to select another game source.
You can also start the executable with `--setup`.
Select an ISO, XISO, ZIP, complete game folder, or `default.xbe` inside that folder.

Setup checks the executable, archives and required movies.
It reports missing, empty, unreadable or invalid required files.
These checks do not detect every modified game asset.
Renaming an unsupported image does not convert it.

To check an image without a game window:

```powershell
ShaolinMonksRecomp.exe --validate-disc "C:/Games/ShaolinMonks/game.xiso.iso"
```

For an executable without the `.exe` extension, use `./ShaolinMonksRecomp`.
Use `--register-disc` to check an image and save its path.
A failed check preserves the previous selection.

ZIP imports use the data folder's `game/` directory.
The importer supports ZIP and ZIP64 with stored or Deflate compression.
It rejects unsafe paths, duplicate names, encrypted archives and corrupt data.
A failed or cancelled import preserves the previous selection.
Extract split archives before you select the game.

## LanguagePack PT-BR

Accept the language-pack prompt to open **Mods** and install or enable **LanguagePack PT-BR**.
If installation fails, check the error in **Mods** before trying again.
For a local `.tar.xz`, use **Install from file...**.
See [language packs](LANGUAGE_PACKS.md) for authoring and diagnostic commands.

The runtime reads the installed pack directly.
It creates no language cache.
It indexes each translated movie on its first read.
That first read can delay playback.

## Mod installation errors

A failed installation preserves the previous mod.
The Mods tab shows the error and the operation status.

| Problem | Action |
| --- | --- |
| Catalog or download unavailable | Check your connection. Try **Check for updates** again. |
| No published version | Install a local release through **Install from file...**. |
| Missing `libcurl.so.4` | Install the system libcurl runtime package. Local package installation does not need it. |
| Download or file hash mismatch | Download the release again. |
| Unsupported game or patch source | Select the original USA/Europe game. |
| Invalid or missing `mod.json` | Use a `.tar.xz` created by the Krypt release builder. |
| Disk full | Free space on the drive that contains the data folder. |
| Files in use | Close other launchers and games before you try again. |

Close other copies before you open the launcher to recover an interrupted installation.
Do not edit `mods/installed.toml` or transaction files manually.

## Logs and crashes

The error dialog identifies the session log.
Close the dialog to open the data folder in the file manager.
You can also select **Open logs** in **Files and support**.
Logs use `logs/` inside the data folder.
Each log records system details, display settings and the port version.

Crash reports use `crashes/` inside the data folder.
A forced termination, power failure or severe process corruption can prevent a report.
Include the session log and the steps that reproduce the problem.
Review crash reports before you share them.
They can contain process memory and local paths.
Do not attach game images or extracted game files.

## Vulkan graphics and audio

The renderer requires Vulkan 1.3, or Vulkan 1.2 with dynamic rendering.
The log records initialization errors and device rejections.
The runtime can continue without graphics after initialization fails.

Install or update the GPU's Vulkan driver.
AMD and Intel GPUs use Mesa drivers.
NVIDIA GPUs use the NVIDIA driver.
The `vulkaninfo --summary` command lists available devices and their capabilities.

Set `SHAOLIN_VK_DEVICE` to part of the GPU name to select a device.
Set `SHAOLIN_VK_VALIDATION=1` to enable an installed Khronos validation layer for renderer diagnostics.
The renderer stores compiled pipelines in `cache/`.
A scene's first run can compile pipelines that later runs reuse.

The ImGui launcher also requires a Vulkan driver.
If that launcher cannot open, the runtime uses saved settings.
A selected custom language still requires an active LanguagePack.
Without an audio device, the SDL audio backend uses silent playback to maintain movie timing.

## Folders

Open **Files and support** to view or change your data and saves folders.
Use `--paths` to display the resolved data paths without moving files.

Default locations:

| System | Data folder | Saves folder |
| --- | --- | --- |
| Windows | `%LOCALAPPDATA%\Shaolin Monks Recomp` | Saved Games folder: `Shaolin Monks` |
| Linux | `~/.local/share/Shaolin Monks Recomp` | `~/.local/share/Shaolin Monks Recomp/saves` |

`$XDG_DATA_HOME` replaces `~/.local/share` when set to an absolute path.
The saves folder contains `hdd/`, including save backups.
The data folder contains settings, logs, cache, crash reports, mods and imported ZIP games.
The runtime preserves the console save structure.
It refuses a save write if it cannot create the required backup.

To move a folder:

1. Close other copies of the game and launcher.
2. Choose an empty destination in **Files and support**.
3. Save your settings.
4. Close the launcher.
5. Open the launcher again.

The launcher records the request in `paths.toml` inside the default data folder.
It checks copied files before it removes the source files.
Data moves also update the selected path of an imported ZIP game.
If preparation fails, the previous folders remain active.
If cleanup fails after the move, the new folders remain active and the launcher reports the remaining source files.

| Option | Effect |
| --- | --- |
| `--data-dir <path>` | Override the data folder for this session |
| `--save-dir <path>` | Use this directory in place of `hdd` |
| `--portable-saves` | Use `hdd` beside the executable |

These options take priority over saved folder preferences.
Background diagnostics use isolated data and saves.
Their default data folder is `runtime-data` beside the executable.

## Settings

Use `--configure` to open only the launcher.
Use `--no-launcher` to start with saved settings.
The launcher asks about unsaved changes before it starts the game or closes.
Decline saving to preserve the settings file.

Enable **Apply these original game options at startup** to apply the settings in **Original game**.
Disable that option to use the game's saved options.
