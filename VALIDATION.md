# Release validation

Validated on 20 October 2026. The final source DLL builds with MSVC/xmake from pinned source dependencies; the bootstrap was separately tested from an empty directory.

## Automated checks

- All 142 profiles (111 shape keys), generated sample caches, distant constants and 80 simulation frames per profile exactly match the previous loader using the effective saved settings. Serialized comparison SHA-256: `570b861632ebb6b1baf9b54f26ded8e70b2bbe9b11a19c1f95118fcb164aa5cf`.
- The public profile regression test initializes profiles on the default executable stack and runs 11,360 simulation frames. The generated initializer was split into bounded functions after this check exposed stack exhaustion.
- Final native objects and shaders pass D3D11 debug-layer tests on NVIDIA RTX 4070 and WARP, with zero D3D errors.
- 72 native HDR scenarios cover formats 10/26/10, same-size format replacement, fractional upscaled viewports, zero/nonzero distortion, foreground occlusion, regional/full-screen/no-UAV copy-back, HDR tint/lighting and alpha preservation.
- 144 Effect-material scenarios check strength, lighting influence, native cubemap sampler/mip behavior and liquid-fade independence.
- Direct distant rendering, near/far endpoints, zero-opacity scattering, corrected mesh-space transformation and state restoration pass.
- 300-geometry stress fixtures verify the existing 256-record limit, overflow returning to the original draw, expiry recovery, missing depth/transform and disabled-renderer behavior.
- Release logging checks verify periodic counts, both target formats, bounded rejection messages and fail-safe errors. No performance timestamp or coverage query code remains in the release sources.
- Removing shader diagnostic branches changes compiler optimization: 9 of 15 comparison images are byte-identical; the other 6 have 108-346 changed channel bytes in 256x256 images, with a maximum display-byte difference of 12/255 around highlights. This is not a claim of pixel-identical shader compilation. Material equations, transforms and render order are retained.
- Main and optional patch meshes are copied byte-for-byte from accepted packages; no new fin separation or geometry edits are introduced.

## Runtime inspection

Address Library mappings were resolved against the provided executable versions. OG/NG executable inspection used private analysis copies; installed executables were not modified and no game code is distributed here.

| Runtime | Batch draw RVA | Effect pass-builder RVA | HDR render RVA | HDR-CS render RVA |
|---|---:|---:|---:|---:|
| 1.10.163 | `0x287EDE0` | `0x27CAE10` | `0x2869130` | `0x28d3e70` |
| 1.10.984 | `0x20C1EE0` | `0x201A600` | `0x20acbd0` | `0x20fc7a0` |
| 1.11.240 | `0x221F080` | `0x21777A0` | `0x2209d70` | `0x22598b0` |

The inspected native Effect refraction branches use pass type 5 and the same utility technique bit construction. The pass geometry pointer and type fields are consistent with the CommonLib definitions. Runtime-specific render-target mapping uses CommonLib accessors, including OG versus NG/AE offsets. The DLL exports both the legacy query and version metadata; standalone query checks accept exactly the three listed versions and reject VR, 1.10.980 and the editor.

**Remaining:** live F4SE loading, hook execution and visual verification of this final 1.0 build in each game runtime. Offscreen GPU tests and successful version queries do not establish complete OG/NG game compatibility. The accepted pre-release was game-tested on 1.11.240, but this release cleanup and OG/NG enablement still need user testing. No new runtime GPU performance claim is made from the offscreen correctness tests.
