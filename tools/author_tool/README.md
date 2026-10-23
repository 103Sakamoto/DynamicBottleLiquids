# DBL patcher 0.9

Open a Fallout 4 NIF, select the liquid volume or bottle wall, calculate a cavity,
inspect the overlay, assign appearance groups, and export a patch ZIP. Processing
is local. No assets are uploaded. Source NIFs are copied, never overwritten.

This is a first authoring version, not a universal NIF converter. It does not
change the current desktop or VR DLLs, shaders or test installation.

## Run

For a packaged Windows build, extract the entire tool ZIP and run
`DBL-patcher.exe`. Keep its `_internal` directory beside it.
The source distribution can also run with Python 3.12+:

```powershell
py -3 -m venv build/author-tool-venv
./build/author-tool-venv/Scripts/python.exe -m pip install -r tools/author_tool/requirements.txt
./tools/author_tool/launch.ps1
```

In this development workspace `launch.ps1` can also use the available bundled
Python and the dependencies in `build/author-tool-deps`. An explicit interpreter
can be supplied with `-PythonPath`.

## Author a bottle

Import original NIFs from their mod's **Meshes** folder. Bottle names and game
paths are derived automatically. Nothing is selected for you. Choose a bottle
from the large list, then follow the numbered steps:

1. **Select shape to be patched.** Select the original liquid volume or glass
   wall. Automatic detection finds the enclosing glass when needed.
2. **Prepare bottle.** Measure the cavity and prepare the supported glass path.
3. **Remove unwanted shapes.** Optionally check redundant fake liquid or other
   shapes to hide in the generated NIF. The patched glass cannot be removed.
   Automatically detected original liquid layers are also hidden on export.
4. **Bottle appearance.** Open the popup from its list row. Each parameter has Save to group and Apply from group.
5. **Review & export.** Validate all bottles, review generated files and save a
   patch ZIP. Test it in game before distribution.

**Advanced geometry** opens a separate window with the 3D preview, cavity view,
navigation toolbar and optional manual controls. Whole numbers and decimals are
accepted for offsets. Requested trim remains unchanged when auto trim is applied.
Changes mark the bottle for recalculation. Closing this window retains edits.

**Group Settings**, beside Import NIF, is the shared-appearance entry point. Assign bottles using the Group
selector. Default appearance is available inside Groups for values that have no
bottle or group override. Pack IDs are generated internally and never requested.

New/Open/Save/Save as, Import and Undo/Redo are directly visible. Color Picker is inside the color palette window.
Keyboard shortcuts still work. Save project keeps drafts, settings and source
copies for later editing; Open project resumes them. Save as makes a portable
copy in a new or empty folder. Unsaved changes prompt before closing. Source
NIFs remain unchanged. Click the adjacent ⓘ buttons for explanations; errors stay visible
in red with an icon. The list expands with the main window and its divider.

## Appearance and groups

Precedence is default appearance, then assigned group, then bottle overrides. Bottles
may have no group. Groups support creation, duplication, rename, editing,
removal and standalone JSON import/export. Removing a group keeps its members'
current appearance as bottle overrides. Changing membership keeps existing
bottle overrides; reset them to inherit the new group's values.

Normalized sliders use **5% steps**. Physical sliders use their displayed units
and 0.1 steps. Numeric fields allow precise values; existing values such as
0.16 are preserved until edited. Native limits remain valid endpoints.

Colors use a round hue/saturation palette, brightness slider, Original/New
swatches and a copy/paste hex field with immediate preview. **Pick from desktop**
temporarily hides the tool and samples from any open reference on your screens.
Click to choose; Esc or right-click cancels. Only RGB is sampled. Screen images
exist only in memory during selection and are never saved. Open Texture and mip
controls have been removed; use your preferred external image viewer.

Wall inset defines the fitted cavity in mesh units. Appearance glass thickness
adjusts that cavity relative to the runtime's nominal 0.16 thickness. Export
clips the cavity at the adjusted ends before checking containment, so closely
spaced end rings cannot reverse when thickness increases. The export adapter
compensates for the existing runtime transformation; no runtime change is needed.
The preview uses the same clipping. The preview is approximate; optical behavior
requires in-game testing. This author tool is not the live in-game editor.

## File separation

An editable author project contains:

```text
project.json                       Source hashes, selections and calculation audit
sources/<profile-id>-<hash>.nif            Copies of the original inputs
geometry/<pack>.<revision>.geometry.json  Identities, transforms and cavity rings only
appearance/<pack>.<revision>.appearance.json  Defaults, groups, memberships and overrides
```

Save commits project.json last, pointing to matching geometry and appearance
revisions. Older revision files remain available for recovery. Legacy author
projects can also be opened.

The geometry validator rejects appearance fields. The editor never saves colors,
fill or optical controls into geometry JSON.

The current external-JSON runtime still reads `profiles/` and `drinks.d/`. Its
compatibility export therefore uses explicit names within those existing paths:

```text
Meshes/<original-path>.nif
F4SE/Plugins/DynamicBottleLiquids/profiles/<pack>.geometry.json
F4SE/Plugins/DynamicBottleLiquids/drinks.d/<pack>.appearance.json
authoring/<pack>.appearance.json   Original editable group definitions
authoring/audit.json
README-author-patch.txt
```

The export adapter resolves author groups into per-mesh appearance for this
runtime; it does not claim that the existing DLL understands arbitrary group
names. Re-export after changing groups. `empty: false` is a required constant in
the legacy geometry adapter; appearance controls are never embedded there.
Fill level zero is controlled through appearance.

**Requires the external-JSON development/VR runtime. The fixed-profile public
1.0 DLL does not load these packs.** Install after the main package and the
original mesh mod; keep the original materials/textures. Existing user tuning
may override pack settings. The tool supplies no DLL, shaders, ESP, BA2 or copied
third-party textures. It does not install anything in a game or mod manager.

## Supported inputs and limits

- Little-endian Fallout 4 NIF 20.2.0.7, stream 130, supported static BSTriShapes.
  Skinned shapes, controlled ancestors and ambiguous multiple parents are rejected.
- Supported Effect targets are normalized to packed layout `0x1b00000430205`.
  Lighting targets receive an embedded Effect property using their maps from
  loose BGSM version 2 materials or an embedded texture set. An unavailable
  external material requires the mod Data folder; BA2 extraction is not included.
  Original source maps remain required. Conversion is structurally validated;
  appearance still needs in-game testing, especially for special material flags.
- Opaque bottle-body overlays, printed labels, unusual material swaps, frost,
  unnamed duplicate liquid-top shapes and fins can require separate asset work. The tool
  does not guess which unrelated parts should be removed or converted.
- Circular cross-sections around one axis, represented by at most 16 rings.
  Handles, badly off-center or non-round interiors, open seams, and overlapping
  surfaces can be rejected. Adjusting the axis/center/end trim may help, but the
  algorithm does not automatically repair an unsuitable mesh.
- The checks use exact in-plane segment distances at finite height samples.
  They are not a proof for every point of an arbitrary mesh. Test upright,
  sideways, inverted and shaking, including the original labels and attachments.
- Unique NIF filenames per pack, since the existing appearance loader keys on
  filenames. Use unique pack/profile IDs across packs and avoid installing a
  replacement patch over another patch for the same mesh without reviewing it.
- Opening an unmodified vanilla Nuka mesh can expose overlapping sections/fins;
  this first version deliberately refuses that fit. It does not reproduce all
  bottle-specific preparation performed by the project's historical builders.

## CLI

From the source repository root (after installing requirements):

```powershell
$env:PYTHONPATH = 'tools'
python -m author_tool inspect path/to/bottle.nif
python -m author_tool fit path/to/bottle.nif --cavity 9 --pack my-mod --id bottle --group my-drinks --mesh-path Meshes/props/Bottle.nif --output build/my-author-project
python -m author_tool export build/my-author-project build/my-patch.zip
```

The block numbers above are illustrative; use the numbers returned by `inspect`.
`--cavity` selects the original liquid; `--target`, `--axis`, and `--asset-root`
are optional manual overrides.

## Existing components reused

- Project NIF reader and shared block helpers from the existing patch builders.
- [Trimesh plane intersections](https://trimesh.org/trimesh.intersections.html)
  for mesh slicing, NumPy for transforms and fitting.
- [PySide6](https://doc.qt.io/qtforpython-6/) and
  [QFluentWidgets](https://github.com/zhiyiYo/PyQt-Fluent-Widgets/tree/PySide6)
  for desktop controls, dialogs and clipboard; Matplotlib Qt embedding for the preview.
- Qt QUndoStack, QSaveFile, QSettings and QScreen for undo, atomic saves, window
  preferences and desktop sampling.
- Current runtime geometry and appearance schemas through a separate export adapter.

The design follows [Fluent 2 layout guidance](https://fluent2.microsoft.design/layout):
4-pixel spacing increments, Segoe UI, clear primary actions, grouped cards and
consistent input controls. It is a Fluent-style Qt application, not a WinUI app
or an official Microsoft Fluent 2 component implementation. Geometry processing
and export are independent from the UI framework.

Project source remains GPL-3.0 under the repository LICENSE. Libraries retain
their own licenses. Author/game assets retain their original rights.

The main cavity preview remains visible beside the movable appearance popup and previews pending changes. Save commits bottle and group edits together; Cancel discards them. Choose a group before import to assign the incoming bottle, or after import to change its membership.


Version 0.7: numeric inputs omit step arrows and unnecessary trailing zeros; keyboard stepping remains available. Appearance and Remove sit together on each bottle row. Click the same help icon again to close its note. Percentage fields display their unit, and sliders share the input row.


The main view uses three columns: shape selection, bottle cards, and cavity preview. Cavity axes use equal physical scale. The modeless appearance editor opens over the left columns so the right preview remains visible. Step numbers have been removed.



Version 0.9 embeds the approved bottle icon in the Windows EXE and uses it for application windows. A stable Windows AppUserModelID identifies DBL patcher on the taskbar.


## Compact distribution (0.9)

The application stays in one-folder mode: no full-bundle extraction occurs at launch. Python reads timezone resources directly from _internal/author-resources.zip through standard zipimport/importlib.resources. Qt resources already embedded in the Python archive remain there. Native DLLs, fonts and resources requiring real file paths remain loose. Unused Qt PDF-image and QML virtual-keyboard components, Qt translation catalogs (the UI is English), and Matplotlib example data are excluded. Normal Windows keyboard input is retained. Third-party notices are preserved in licenses.zip; open this archive to read them.

