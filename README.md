# Dynamic Bottle Liquids

Dynamic liquid, sloshing, bubbles, surface foam and native scene refraction for supported Fallout 4 bottles.

**[Download and install from Nexus Mods](https://www.nexusmods.com/fallout4/mods/110181).** DLLs, compiled shaders, meshes and installable archives are not hosted on GitHub.

## Version 1.1

The 1.1 installer brings together desktop and VR builds, vanilla bottle replacers and additional drink patches. It includes material-based appearance overrides, carbonation and shaking bubbles, surface foam for carbonated drinks, and the revised wet film inside the glass.

Rendering retains full-resolution native refraction, foreground depth guards, regional composition/copy-back and distance-based optimizations. Glass reflections remain independent of liquid distance fading. Both R16G16B16A16_FLOAT and R11G11B10_FLOAT scene-colour buffers are supported.

External drink and material profiles are available again in 1.1. Desktop F7 tuning remains available. Public packages default to **Basic logging**; detailed diagnostics can be enabled through the INI below.

## Requirements and installation

| Installer choice | Supported game executables |
|---|---|
| Desktop Fallout 4 — one DLL for OG / NG / AE | 1.10.163, 1.10.984, 1.11.240 |
| Fallout 4 VR — separate DLL | 1.2.72 |

Use the matching F4SE or F4SEVR build, the Address Library/database for your executable, and the Microsoft Visual C++ x64 runtime.

Install through MO2 or Vortex. Choose one runtime, then select only patches for the original mods you have installed. Choose **Vanilla bottles** if you do not use a Nuka-Cola replacer. Let this package overwrite the corresponding bottle assets from the original mods. Replace older Dynamic Bottle Liquids core/patch packages rather than layering them together. Back up personal settings before upgrading: mod-manager Overwrite files may take precedence over the packaged configuration.

The installer includes these optional patches; their original mods are required:

- The Nuka Project
- Nuka-Cola Classic — Nuka World DLC variant
- F4NV - Neo Classic Nuka Cola
- Oppressor's Nuka-Cola Project (4K) - Complete Edition (BOS Support)
- The Vim Project
- Beer Bottles Retextured
- Star Size Sunset Sarsaparilla - A Soda Mod
- Atomic Shine - A Nuka-Shine Mod
- Vintage Nukas - Powerful New Sodas

**IAF (Immersive Animation Framework):** core animation-object meshes are included automatically. The Nuka Project and The Vim Project choices also include their matching IAF variants. IAF itself is not bundled and is not required for world bottles. This does not imply that every optional replacer or VR animation framework has a matching IAF patch.

## Logging and troubleshooting

Edit `Data/F4SE/Plugins/DynamicBottleLiquids.ini`, then restart the game:

```ini
[Logging]
Mode=Basic
```

- **Basic** is the public default. It records startup/runtime information, profile counts, bounded failure messages and periodic health counts without the performance panel or profiling queries.
- **Extensive** enables detailed logs and the diagnostic panel, including GPU profiling sampled once every 16 bottle-active frames. Development packages default to this mode. Profiling adds overhead.

The INI supersedes the older JSON `diagnostics` switch. F4 is no longer used. In Extensive mode, F8 hides/shows the panel and F9 resets measurements; hiding the panel does not stop profiling.

The log is normally at `Documents/My Games/Fallout4/F4SE/DynamicBottleLiquids.log`; the save-folder name can differ for VR or custom installations. Health counts describe draw operations, not unique bottles or frame time.

When reporting a problem, include the full plugin log, game/F4SE versions, relevant rendering mods or upscaler, and screenshots. For a crash, also include the complete crash log.

**Known limitations:** ENB can still lose liquid refraction with multiple bottles. A reported third-party D3D11 context-vtable crash is being investigated; 1.1 does not include a speculative fix for that report.

**Validation:** desktop and VR 1.1 builds compile, eight local regression/GPU tests pass, and all 320 installer configurations pass the configuration-loader checks. These checks do not replace live testing of every runtime, headset or mod combination. The final 1.1 package still needs in-game verification.

## Credits

- **[brunocatani (Bruno Catani)](https://github.com/brunocatani)** — thank you for helping with VR development. The VR integration builds on the work in the **[DynamicBottleLiquidsVR `fallout4vr` branch](https://github.com/brunocatani/DynamicBottleLiquidsVR/tree/fallout4vr)**.
- **[L0rns on Nexus Mods](https://forums.nexusmods.com/profile/86122018-l0rns/)** — thank you for testing and helping validate the mod.
- Thanks to the original bottle/drink mod authors and the maintainers of the libraries used by this project. Their assets and dependencies retain their own licenses.

## Source and license

Project code is licensed under the included [GNU GPL v3 license](LICENSE). Game/mod assets in separately distributed install packages retain their original rights; this license does not relicense those assets.

This README describes the 1.1 package. The repository source and [build notes](BUILD.md) currently describe the earlier published source revision; the separate 1.1 source archive accompanies the prepared release and has not yet been synchronized here. See the existing [third-party notices](THIRD_PARTY.md) for that revision. The 1.1 source package additionally retains the VR contributor and dependency notices.
