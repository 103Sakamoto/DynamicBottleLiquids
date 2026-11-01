# Third-party components

This page distinguishes the published DBL 1.3 distribution from the earlier runtime source currently on GitHub main. The 1.3 desktop package directly uses Fusion UI and Fusion Framework; they are not merely inspiration or reference material.

## Fusion UI and Fusion Framework (desktop 1.3)

Copyright (C) 2026 Bryant-21 and respective contributors. Both projects are distributed under **GNU GPL version 3 or, at your option, any later version** (`GPL-3.0-or-later`).

| Component | Upstream revision used for the release build | Role |
|---|---|---|
| [Fusion UI](https://github.com/Bryant-21/fusion-ui-fo4) | [`6e81441a667a478635f5852a5f9b946da318d15e`](https://github.com/Bryant-21/fusion-ui-fo4/tree/6e81441a667a478635f5852a5f9b946da318d15e) | Shared UI and input infrastructure for DBL's editor and overlay |
| [Fusion Framework](https://github.com/Bryant-21/fusion-framework-fo4) | [`82e79b2e8130bdb13bb3ec1c58d2b35185879ce5`](https://github.com/Bryant-21/fusion-framework-fo4/tree/82e79b2e8130bdb13bb3ec1c58d2b35185879ce5) | Framework services required by the pinned Fusion integration |

DBL's `FusionHost.cpp` registers `DBL Appearance` and `DBL Performance` as Fusion UI clients. The appearance client is modal and pauses the game; the performance client is passive and does not pause gameplay. DBL owns its bottle-specific controls and settings model. Fusion owns the shared UI/input integration. Fusion UI uses Dear ImGui, whose own MIT notice remains applicable.

The desktop installer includes separate `FusionUI.dll` and `FusionFramework.dll` files plus UI resources. This packaging description applies to the pinned DBL build, not necessarily to current upstream builds. The unchanged VR runtime does not include these desktop components.

Distribution records and notices:

- Installer: `licenses/FusionUI-GPL-3.0.txt` and `licenses/FusionFramework-GPL-3.0.txt`.
- UI resources: font/icon notices retained beneath `F4SE/Plugins/FusionUI/`; these resources retain their individual licenses.
- Corresponding source: `DynamicBottleLiquids-1.3-Source.zip`, with Fusion UI under `deps/fusion-ui/` and Fusion Framework under `deps/fusion-ui/extern/fusion-framework-fo4/`.
- `provenance.json` records the upstream revisions. Working-tree patch records and the actual source snapshots are included; the build is not represented as an unmodified checkout solely on the basis of a commit ID.
- Fusion's `README.md`, `THIRD_PARTY.md`, license texts, and other upstream notices remain part of the source snapshots.

The **[corresponding 1.3 source archive](https://github.com/103Sakamoto/DynamicBottleLiquids/releases/download/v1.3/DynamicBottleLiquids-1.3-Source.zip)** is published with the [1.3 release](https://github.com/103Sakamoto/DynamicBottleLiquids/releases/tag/v1.3), alongside `Fusion-NOTICES.txt` and checksums. This attached archive supplies the release source; the older runtime source on GitHub main and GitHub's automatic source archives are not the corresponding 1.3 snapshot.

## Other runtime dependencies

The pinned earlier desktop source uses:

| Component | Revision | License |
|---|---|---|
| [CommonLibF4](https://github.com/Dear-Modding-FO4/commonlibf4) | `01cd60c70a6a2aa79e7fd01b3be577df55fc7235` | GPL v3; retain its accompanying terms/notices |
| [commonlib-shared](https://github.com/Dear-Modding-FO4/commonlib-shared) | `12e1c6a45623f9740a3d866f359d547ef94b5096` | GPL v3 |
| [DearModdingUI-API](https://github.com/Dear-Modding-FO4/DearModdingUI-API) | `04b1f40683c0b11bcd696c8df76db5157ec3a3a8` | GPL v3 |
| [MinHook](https://github.com/TsudaKageyu/minhook) | 1.3.4 | BSD 2-Clause, including bundled disassembler notices |
| [spdlog](https://github.com/gabime/spdlog) | 1.16.0 | MIT |

The current external-profile/settings implementation also uses **nlohmann/json (MIT)**. The older statement that DBL no longer uses JSON applies to the earlier frozen-settings release, not the 1.3 build.

VR integration derives from **Bruno Catani's [DynamicBottleLiquidsVR fallout4vr branch](https://github.com/brunocatani/DynamicBottleLiquidsVR/tree/fallout4vr)**, revision `ec796e180804763c47046f9a81cca317ac0d561f` (GPL-3.0). Its dependency notices cover CommonLibF4VR, MinHook, spdlog, fmt, rapidcsv, rsm-mmio, and nlohmann/json. The 1.3 installer retains the 1.2 VR DLL and shaders unchanged.

Use the license files supplied with the relevant binary/source distribution for the full terms. This summary does not replace those notices.

## Original mod assets

Fallout 4 assets and optional bottle, drink, animation, and installer-image assets retain their original authors' rights. Optional compatibility packages require the corresponding original mods. The DBL project license does not relicense those assets.
