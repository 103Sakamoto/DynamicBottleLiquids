# Building DBL patcher 0.9 on Windows

This guide is for the standalone Python/Qt **DBL patcher 0.9**, not the Fallout 4
native plugin. The accompanying application source is the snapshot used for the
0.9 Windows upload. `source-manifest-0.9.json` records the original source-file
hashes and the SHA-256 of both distributed archives.

## Prerequisites

- Windows x64, PowerShell, Git, and Python **3.12 x64** (the upload used 3.12.14).
- Internet access to install Python build dependencies. The application itself
  processes local files; it does not need a server or an API key.
- No Fallout 4 installation, Visual Studio, F4SE SDK, or game assets are needed
  to compile the EXE. A legally obtained compatible NIF is needed for the optional
  end-to-end prepare/export check.

Run these commands from the repository root. The version branch below contains
this source snapshot; immutable commit links can also be used for review.

```powershell
git clone --branch patcher-v0.9 https://github.com/103Sakamoto/DynamicBottleLiquids.git
cd DynamicBottleLiquids
py -3.12 -m venv .venv
$python = (Resolve-Path .venv/Scripts/python.exe).Path
& $python -m pip install --upgrade pip
New-Item -ItemType Directory -Force build | Out-Null
& $python -m pip install --target build/author-tool-deps -r tools/author_tool/requirements-build-0.9.txt
& ./tools/author_tool/build.ps1 -PythonPath $python
```

If PowerShell blocks this local build script, run that final command through
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/author_tool/build.ps1 -PythonPath $python`.
This changes execution policy only for that child process.

The output is **`build/author-tool-dist/DBL-patcher/DBL-patcher.exe`**. Run it with
its entire adjacent `_internal` directory; it is a one-folder application.

The lock file records the versions installed for the uploaded build, including
PyInstaller and its hooks. It pins versions, not wheel hashes. It documents the
build environment; byte-for-byte reproducibility is not claimed (Python builds,
PE/ZIP timestamps, and wheel/platform details may differ).

## Source tests

```powershell
$env:PYTHONPATH = "$PWD/build/author-tool-deps;$PWD/tools;$PWD/build/author-tool-deps/win32;$PWD/build/author-tool-deps/win32/lib;$PWD/build/author-tool-deps/pywin32_system32"
$env:MPLCONFIGDIR = "$PWD/build/mpl-cache"
New-Item -ItemType Directory -Force build/test-temp | Out-Null
$env:TEMP = "$PWD/build/test-temp"
$env:TMP = $env:TEMP
& $python -m unittest discover -s tests -p 'test_author*.py'
```

Tests requiring local third-party NIFs skip when those fixtures are unavailable.
Those meshes are deliberately not redistributed with this source.

## Optional packaged workflow check and distribution ZIP

Choose a suitable original bottle NIF and identify its liquid shape block using
`inspect`. Paths and block numbers below must be replaced with your own input.

```powershell
& $python -m author_tool inspect 'C:/path/to/Meshes/Bottle.nif'
$exe = (Resolve-Path build/author-tool-dist/DBL-patcher/DBL-patcher.exe).Path
# Example block 9: use the correct block shown by inspect for your NIF.
Start-Process -FilePath $exe -ArgumentList 'self-test "C:/path/to/Meshes/Bottle.nif" "build/author-tool-packaged-smoke.json" --liquid 9' -Wait
Get-Content build/author-tool-packaged-smoke.json
& $python tools/author_tool/package.py
```

Use an absolute report path if running the EXE with a different working directory.
`package.py` requires a successful **0.9** report and creates
`build/DBL-patcher-0.9-Windows.zip` and `build/DBL-patcher-0.9-source.zip`.
The workflow check exercises preparation, export, dialogs, source-file preservation,
and timezone access directly from the resource ZIP. It is not a malware scan or
an in-game compatibility certification.

## Packaging details for reviewers

- Entry point: `tools/author_tool_entry.py`; application: `tools/author_tool/`.
- NIF parsing helpers: `tools/nif_mesh.py` and `tools/nif_document.py`.
- Build definition: `patcher.spec`; standard PyInstaller **one-folder**, not one-file.
- `_internal/author-resources.zip` contains timezone package resources read using
  Python's standard ZIP importer and `importlib.resources`. `runtime_resources.py`
  adds that archive to the import path; it does not extract it at startup.
- `licenses.zip` preserves third-party license notices. Nested ZIPs are intentional.
- Native DLLs/fonts that require filesystem paths remain loose. Unused Qt PDF-image,
  QML virtual-keyboard components, translation catalogs and plotting example data
  are removed by the explicit filters in `patcher.spec`.
- The supplied icon PNG/ICO are application artwork, not executables. PyInstaller
  embeds the ICO in the EXE; Qt uses it for application windows.
- No mod/game meshes, textures, compiled game plugins, or credentials are included
  in this source publication. Project code is GPL-3.0; dependencies keep their own licenses.
