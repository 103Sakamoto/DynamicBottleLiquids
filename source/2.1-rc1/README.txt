Dynamic Bottle Liquids 2.1 RC1 - OG / NG / AE
Unpublished release candidate, 10 November 2026. User testing required.
Supported: Fallout 4 1.10.163, 1.10.984 and 1.11.240. No VR payload.
Install the complete FOMOD through MO2/Vortex and choose your existing settings UI.
Requires matching F4SE, Address Library and the selected UI host (if any).
Keep original bottle mods enabled; select only their matching DBL patches.
Acid Soaker support is automatic when Nuka-World is active.

Upgrade: preserve UserSettings and personal acid-soaker-bubbles.json edits.
Disable older DBL core/test overlays, including Diagnostic 1/2, mixed-drink ESP,
Novelty rollback, and earlier asset-test overlays now incorporated here.
This installer contains no player tuning.json/ui.json or UserSettings files.
Restart the game fully. Keep the previous mod disabled for easy rollback.

Known pending issues:
- Permanent Cobalt SSR Fix compatibility is NOT implemented. The verified workaround
  for SSR Fix 1.10.1 is [Mirror] iReHook=0 in SSRFix.ini BESIDE its actual DLL
  (the original SSR mod directory in MO2). An INI-only override may not be read.
  SSR Fix can automatically re-enable re-hooking; longer-session/reflection checks
  remain needed. This package does not change SSR Fix files or settings.
- Original public 2.0/DLSS crash report awaits reporter isolation tests and new logs.
  This is separate from the accepted F4R Upscaling startup fix.
- OG/NG/AE are build targets; full runtime/UI combinations still need RC testing.

Test: startup; inventory list and X inspection; world bottles; warm/cold variants;
Supa Yum and patched materials; Vintage labels; whiskey cork/collar; Visible Favorites
for several minutes; F4R startup; Refreshing Beverage/Dirty Wastelander and empty junk.
With SSR Fix installed, retain the verified workaround and check reflections too.
Report runtime, settings UI, selected patches and DBL/crash logs for any failures.
No publication or installation was performed by the build process.
