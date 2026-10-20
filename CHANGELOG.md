# 1.0.0 (unreleased corrections)

- Restored renderer initialization accidentally removed alongside a profiling call during release cleanup. Initialization completes before the draw hook is enabled, and success is logged.
- Removed the 26 `Meshes/props/nuka world/` meshes from the main package. The Nuka Project patch retains its own meshes.
- No changes to shader equations, frozen settings, or mesh contents.

# 1.0.0

- Consolidated the accepted 0.13.4.37 rendering behavior and current effective appearance settings.
- Removed tuning UI, hotkeys, JSON overrides, detailed profiling and unused legacy shaders/helpers.
- Retained lightweight startup, failure and operation-count logging.
- Added exact OG 1.10.163 and NG 1.10.984 runtime declarations alongside 1.11.240, using the pinned CommonLib runtime-specific address/layout accessors. OG/NG live testing remains outstanding.
- Preserved both native HDR colour formats, full-resolution refraction, regional composition, distance fading, foreground protection, bubbles and surface lighting.
- Preserved all main, Nuka Project and Vim Project mesh bytes from the accepted packages.
