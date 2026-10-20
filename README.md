# Dynamic Bottle Liquids

Dynamic liquid, sloshing, bubbles and native scene refraction for supported Fallout 4 bottles.

**Download and install from [Nexus Mods](https://www.nexusmods.com/fallout4/mods/110181).** This repository contains source only. DLLs, compiled shaders, meshes and installable archives are not hosted on GitHub.

## Version 1.0

The release fixes the approved appearance and simulation settings in the plugin. There is no settings panel, hotkey, JSON override or detailed performance profiler. Basic compatibility logging remains enabled.

The native optical path retains full-resolution rendering, foreground depth guards, regional composition/copy-back and the 512–768 unit distance transition. Glass reflections remain separate from liquid distance fading. Both R16G16B16A16_FLOAT and R11G11B10_FLOAT scene colour buffers are supported.

## Requirements and installation

- Windows x64 and Fallout 4 **1.10.163 (OG), 1.10.984 (NG), or 1.11.240**.
- The F4SE build matching your executable, plus Address Library for F4SE Plugins with the database matching that executable.
- Microsoft Visual C++ x64 runtime.
- Install the main archive through your mod manager and launch through F4SE. Replace the old DynamicBottleLiquids installation rather than merging development files into it.
- If using The Nuka Project or The Vim Project, install the corresponding optional patch after the main archive and after the original mod. The patch does not replace its parent mod.

The approved 0.13.4.1 Nuka/Vim patch meshes remain compatible and are unchanged in the 1.0 patch packages. The main archive excludes `Meshes/props/nuka world/`; those meshes belong to the Nuka Project patch. Existing tuning JSON files are ignored. Supported meshes continue to use their existing texture paths.

**Validation status:** OG/NG hook locations and native pass construction were inspected in the supplied executables. Offscreen D3D11 tests pass on hardware and WARP. On 20 October 2026, the tester confirmed that corrected 1.0.0 works in their current setup (the latest supplied log identifies Fallout 4 1.11.240). Live OG and NG testing remains outstanding; executable inspection is not a substitute for it. Fallout 4 VR and other executable versions are not enabled.

## Troubleshooting

See `Documents/My Games/Fallout4/F4SE/DynamicBottleLiquids.log` (the save-folder name may differ with the runtime).

The log records startup/runtime and profile counts, optical target dimensions/format, bounded capture rejection messages, initialization/fail-safe errors and a health summary every 1,800 observed bottle frames. Counts describe draw operations, not unique bottle references or frame time. No GPU timestamp or coverage queries are issued by the release.

When reporting an incompatibility, include this log, your executable/F4SE versions, relevant rendering mods/upscaler, and a screenshot or capture. Unsupported captures retain the original draw. If native pass registration/composition repeatedly fails, the optical path disables itself for the session.

## Source and license

See [BUILD.md](BUILD.md), [VALIDATION.md](VALIDATION.md) and [THIRD_PARTY.md](THIRD_PARTY.md). Project source is provided under the included GNU GPL v3 license. Game/mod assets in separately distributed install packages retain their original rights; this source license does not relicense them.
