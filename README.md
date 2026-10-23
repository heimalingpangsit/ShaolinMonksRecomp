# Shaolin Monks Recomp

<p align="center">
  <img src="docs/images/shaolin-monks-recomp.png" width="760" alt="Shaolin Monks Recomp logo">
</p>

<p align="center">
  <a href="https://github.com/OAleex/ShaolinMonksRecomp/releases/latest"><img src="https://img.shields.io/badge/Download-Latest-2563eb?style=for-the-badge&amp;logo=github&amp;logoColor=white" alt="Download the latest release"></a>
  <a href="https://github.com/OAleex/ShaolinMonksRecomp/releases/latest"><img src="https://img.shields.io/github/v/release/OAleex/ShaolinMonksRecomp?style=for-the-badge&amp;logo=github&amp;label=Latest%20release&amp;cacheSeconds=300" alt="Latest stable release"></a>
  <a href="https://github.com/OAleex/ShaolinMonksRecomp/releases"><img src="https://img.shields.io/github/downloads/OAleex/ShaolinMonksRecomp/total?style=for-the-badge&amp;logo=github&amp;label=Release%20downloads" alt="Total GitHub release downloads"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--2.0--or--later-blue?style=for-the-badge" alt="GPL-2.0-or-later license"></a>
</p>

> [!IMPORTANT]
> Shaolin Monks Recomp does not include the original game or its assets.
> You must provide your own legally obtained **USA/Europe Xbox copy of Mortal Kombat: Shaolin Monks**.

**A PC port of Mortal Kombat: Shaolin Monks, made through static recompilation.**

The original Xbox code runs natively on the PC.
The port adds resolution and render scale settings, display modes, keyboard and mouse input, and remappable controller buttons.

The maintainer reports a completed campaign playthrough. Some graphical issues remain.

**Mortal Kombat II** and **The Suffering Demo** do not run.

## System requirements

| Component | Requirement |
| --- | --- |
| CPU | x64 processor with SSE4.2 and POPCNT |
| System | Windows 10 version 1903 or later, or Linux with glibc 2.38 or later |
| GPU | Direct3D 12, or Vulkan 1.3. Vulkan 1.2 requires dynamic rendering. |
| Input | Keyboard and mouse, or a compatible controller |
| Game files | Original USA/Europe Xbox release |

## Getting started

1. Download the [latest release package](https://github.com/OAleex/ShaolinMonksRecomp/releases/latest) for your system.
2. Extract the package.
3. Start the executable in the extracted folder.
4. Select your ISO, XISO, ZIP, extracted game folder, or `default.xbe`.
5. Select **Save and Play** in the launcher.

The runtime reads ISO and XISO images directly. Setup imports ZIP contents once.
Keep the selected image or complete game folder available. You can change it in the launcher's **Files and support** tab.

## Controls

Xbox, PlayStation, and other SDL-compatible controllers support connection and disconnection during play.
DualShock 4 works through USB or Bluetooth. The game displays Xbox button prompts.

| Xbox input | Keyboard | Mouse | PlayStation |
| --- | --- | --- | --- |
| Left stick | W / A / S / D | - | Left stick |
| Direction pad | Arrow keys | - | Direction pad |
| A | Space | - | Cross |
| B | Esc or Ctrl | Right button | Circle |
| X | J | Left button | Square |
| Y | K | Middle button | Triangle |
| White | Q | Side button 2 | L1 |
| Black | C | Side button 1 | R1 |
| Left trigger | Shift | - | L2 |
| Right trigger | E | - | R2 |
| Start | Enter | - | Options |
| Back | Backspace | - | Share |
| Right stick | - | Movement | Right stick |
| FPS and frame time | F9 | - | - |
| Release / capture pointer | F10 | Click to capture | - |

See [controller settings](docs/CONTROLLERS.md) for mappings, vibration, and connection details.

## Languages

| Language | Interface | Voice acting |
| --- | --- | --- |
| English | Available | Available |
| French | Available | Available |
| German | Available | Available |
| Spanish | Available | Available |
| Italian | Available | Available |
| Brazilian Portuguese | Available | Available |

Some languages require a [language pack](docs/LANGUAGE_PACKS.md).

## Mods

The launcher reads the official mod catalog from the [Krypt](https://github.com/OAleex/ShaolinMonksRecompKrypt).
It checks for mod updates when it opens.
Manage installed mods in **Mods**, or use **Install from file...** for a local `.tar.xz` package.
A failed installation preserves the previous mod.
New mods and languages appear from the catalog. See [mod authoring](docs/MODS.md) for supported content and routine changes.

## User files

Open **Files and support** to view your data and saves folders.

| Folder | Contents |
| --- | --- |
| Data | Settings, logs, cache, crash reports, mods and imported ZIP games |
| Saves | The `hdd` folder, including save backups |

To move a folder, close other copies of the game and launcher first.
Choose an empty destination in **Files and support**. Save your settings. Restart the launcher to move the files.
See [troubleshooting](docs/TROUBLESHOOTING.md#folders) for default paths and command-line options.

Error dialogs identify the session log. Include that log when you report a problem.

## Credits

- [TGP482 / XboxXBERecomp](https://github.com/TGP482/XboxXBERecomp): static recompiler and Xbox runtime.
- [LLVM / Clang](https://llvm.org/): native code compilation.
- [Ghidra](https://github.com/NationalSecurityAgency/ghidra): executable analysis and function inspection.
- [SDL](https://github.com/libsdl-org/SDL): controller input, device detection and platform services.
- [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) and [glslang](https://github.com/KhronosGroup/glslang): graphics headers and shader compilation.
- [Dear ImGui](https://github.com/ocornut/imgui): host interface and performance display.
- [toml++](https://github.com/marzer/tomlplusplus): configuration files.
- [miniz](https://github.com/richgel999/miniz): ZIP imports and language resource decompression.
- [XZ Embedded](https://github.com/tukaani-project/xz-embedded): mod package decompression.
- [Lua](https://www.lua.org/): mod routine scripts.
- [xemu](https://xemu.app/), [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded), and [nxdk](https://github.com/XboxDev/nxdk): behavior and ABI references.
- [OAleex Traduções](https://oaleextraducoes.com/posts/2023/10/Mortal-Kombat-Shaolin-Monks/mortal-kombat-shaolin-monks-xbox/): translation and dubbing. See the [contributors](docs/TRANSLATION_CREDITS.md).

See [BUILDING.md](docs/BUILDING.md) to build from source, or [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) to report a problem.

## AI usage

The development process uses AI assistance for executable analysis, code changes, debugging, tests, build scripts, and documentation.
The maintainer reviews these contributions. Validation includes automated tests and gameplay checks.

## License

The project code uses **GPL-2.0-or-later**. See [LICENSE](LICENSE), [tools and dependencies](UPSTREAM.md), and [dependency notices](licenses).
The project license does not grant rights to the original game or its artwork.
This project is not affiliated with or endorsed by the owners of Mortal Kombat or Xbox.
