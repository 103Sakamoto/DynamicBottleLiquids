# Dynamic Bottle Liquids

Moving liquid, sloshing, carbonation and shaking bubbles, surface foam, wet inner glass, and scene refraction for supported Fallout 4 bottles.

**[Download from Nexus Mods](https://www.nexusmods.com/fallout4/mods/110181)** or see the [GitHub releases](https://github.com/103Sakamoto/DynamicBottleLiquids/releases). The runtime mod and standalone DBL Patcher have separate version numbers.

## Version 1.3

- Fixed incorrect glass rendering at **0% glass opacity**.
- Improved the desktop appearance editor with Fusion UI, full-screen mouse movement, and 1% adjustment steps.
- Bottles blend more naturally into fog and god rays, with smooth automatic atmospheric fading.
- Includes inventory/inspection transparency, glass texture-binding, and overly bright reflection fixes.
- Added **Modern Replacer - Nuka-Cola** glass-bottle support; cans are unchanged.
- Reduced **Star Size Sunset Sarsaparilla** reflection strength to 0.25.
- Fixed the **Duplicate tuning mesh** settings-loading error during upgrades, preserving saved settings.
- Retains the 1.2 compatibility patches, 95% Nuka fill, and saved customization.

**[Download version 1.3](https://github.com/103Sakamoto/DynamicBottleLiquids/releases/tag/v1.3)**. The release includes the full installer, corresponding source, Fusion notices, and checksums. VR retains the unchanged 1.2 runtime. The new Fusion editor and atmospheric fading are desktop-only.

## Requirements and installation

| Runtime | Supported executable versions |
|---|---|
| Desktop Fallout 4 | 1.10.163, 1.10.984, 1.11.240 |
| Fallout 4 VR | 1.2.72 |

Use matching F4SE/F4SEVR, the Address Library for your executable, and the Microsoft Visual C++ x64 runtime. Select one runtime in the MO2/Vortex installer and only compatibility patches for mods you have installed. Original bottle mods remain required. Choose vanilla bottles if you do not use a Nuka-Cola replacer, and let the selected DBL assets win conflicts.

Replace older DBL core/patch packages and disable superseded test overlays. Keep your **UserSettings** folder when updating. Existing overrides take priority over new factory defaults.

Compatibility options include The Nuka Project, Nuka-Cola Classic, Neo-Classic Nuka Cola, Oppressor's Nuka-Cola Project, The Vim Project, Beer Bottles Retextured, Star Size Sunset Sarsaparilla, Atomic Shine, Vintage Nukas, Novelty-Nuka Redux, Wasteland Imports, and Modern Replacer - Nuka-Cola. Select only the options supplied by your installer version.

IAF bottle props are supported where supplied by the core or selected patch. IAF itself is not bundled and is not required for world bottles. Support for one bottle family does not imply support for every animation or replacer.

## Desktop appearance editor

Pick up a supported bottle and press **Ctrl+F7**. The editor pauses gameplay while you adjust appearance. Save settings to a bottle or group, restore group inheritance, and manage custom groups. **Ctrl+F8** toggles the passive performance overlay; shortcuts can be customized. VR does not include this editor.

Personal settings are stored in:

`Data/F4SE/Plugins/DynamicBottleLiquids/UserSettings/`

This folder contains appearance/group settings (`tuning.json`), shortcuts (`ui.json`), and logging preferences. Existing preferences migrate on first run without overwriting settings already saved there. Mod managers may redirect writes to their overwrite/output folder. Installers do not include personal settings.

## How DBL uses Fusion UI

The **1.3 desktop editor and performance overlay use [Fusion UI](https://github.com/Bryant-21/fusion-ui-fo4) and [Fusion Framework](https://github.com/Bryant-21/fusion-framework-fo4), created by [Bryant-21](https://github.com/Bryant-21) and contributors**. These are third-party software dependencies, not original DBL frameworks.

DBL registers its appearance editor and passive overlay as Fusion UI clients. DBL supplies the bottle controls and settings; Fusion provides the shared UI/input infrastructure, including cursor and input ownership and the editor's pause behavior. Fusion UI builds on Dear ImGui.

The 1.3 desktop package includes `FusionUI.dll`, `FusionFramework.dll`, and the required UI resources. They are not used by the VR runtime. If another mod provides these shared components, avoid overwriting a newer compatible version with an older copy.

Fusion UI and Fusion Framework are **GPL-3.0-or-later**, copyright 2026 Bryant-21. Their license texts and font/icon notices are retained in the distribution. The attached 1.3 source archive includes the source snapshots, dependency provenance, and working-tree patch records used for the build. See [third-party notices](THIRD_PARTY.md) for exact revisions and notice locations. This describes DBL's packaged integration; upstream Fusion's current architecture may differ from the pinned revision.

## Logging and troubleshooting

**Basic** is the default logging mode. **Extensive** enables detailed diagnostic profiling and adds overhead. Change `[Logging] Mode=Basic` to `Mode=Extensive` in the logging INI and restart the game. A saved `UserSettings/DynamicBottleLiquids.ini` takes priority over the shipped `Data/F4SE/Plugins/DynamicBottleLiquids.ini`. Opening the overlay does not automatically enable profiling.

When reporting a problem, include the full DBL log, game/F4SE versions, relevant rendering mods or upscalers, and screenshots. For a crash, include the complete crash log. The log is normally under `Documents/My Games/Fallout4/F4SE/DynamicBottleLiquids.log`; VR/custom setups can use a different save-folder name.

ENB multi-bottle refraction remains a known limitation. Automated build/configuration tests do not certify every executable, headset, or mod combination. The 1.3 installer passed all 1,536 configuration selections; live final-package VR testing is not claimed.

## DBL Patcher

The standalone bottle-authoring tool imports supported NIFs, prepares cavity profiles, and exports compatibility patches. Download [DBL Patcher 1.0](https://github.com/103Sakamoto/DynamicBottleLiquids/releases/tag/patcher-v1.0), read its [guide](tools/author_tool/README.md), or follow the [build instructions](tools/author_tool/BUILD-1.0.md). Extract the full Windows ZIP and keep `_internal` beside the executable.

## Credits

- **Bryant-21 and Fusion contributors** — Fusion UI and Fusion Framework, used by the 1.3 desktop editor and overlay.
- **[brunocatani (Bruno Catani)](https://github.com/brunocatani)** — VR development and the [DynamicBottleLiquidsVR fallout4vr branch](https://github.com/brunocatani/DynamicBottleLiquidsVR/tree/fallout4vr) on which the VR integration builds.
- **[L0rns](https://forums.nexusmods.com/profile/86122018-l0rns/)** — testing and validation.
- The F4SE/Address Library teams, CommonLib contributors, Dear ImGui authors, and maintainers of the other libraries listed in [THIRD_PARTY.md](THIRD_PARTY.md).
- The original bottle, drink, and animation mod authors for their assets and integrations.
- Valve's *Half-Life: Alyx* for inspiration.
- ChatGPT / OpenAI Codex for AI-assisted development, research, debugging, testing, and documentation, guided and reviewed by the mod author.

## Source and license

DBL project code uses the included [GNU GPL v3 license](LICENSE). Third-party software and game/mod assets retain their own licenses; DBL's license does not relicense those assets.

**[Download the corresponding 1.3 source](https://github.com/103Sakamoto/DynamicBottleLiquids/releases/download/v1.3/DynamicBottleLiquids-1.3-Source.zip)** for the desktop/VR sources, Fusion dependency snapshots, build instructions, provenance, and notices. The source currently on GitHub main and its [build notes](BUILD.md) describe an earlier runtime revision. GitHub's automatically generated source ZIP/tarball follows the repository tag; use the explicitly attached `DynamicBottleLiquids-1.3-Source.zip` to reproduce the 1.3 build. See the release's `Fusion-NOTICES.txt` and `SHA256SUMS.txt` for notices and checksums.
