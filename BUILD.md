# Building from source

Use Windows x64, Visual Studio with C++ desktop tools (tested with MSVC 2026), a Windows 10/11 SDK containing `fxc.exe`, Git, and xmake 3.0 or later. A network connection is needed for initial dependency setup.

From PowerShell in the repository:

```powershell
./bootstrap.ps1
xmake f -m release -y
xmake -j 4 DynamicBottleLiquids
./build-shaders.ps1
xmake -j 4 ProfileTests
xmake run ProfileTests
```

The DLL is written to `build/bin/DynamicBottleLiquids.dll`. Eleven production shader binaries are written to `build/shaders`. For a local installation, the DLL belongs in `Data/F4SE/Plugins/` and the shaders in `Data/F4SE/Plugins/DynamicBottleLiquids/shaders/`. Obtain the supported meshes from the Nexus package. Source builds do not generate or redistribute Bethesda or third-party meshes.

`bootstrap.ps1` checks out pinned CommonLibF4, commonlib-shared and DearModdingUI-API revisions in `.deps`. xmake supplies pinned MinHook 1.3.4 and spdlog 1.16.0. The plugin uses the API headers through CommonLibF4; it does not require the Dear Modding UI mod at runtime.

Appearance and geometry profiles are compiled from `native/src/FrozenProfiles.inc`. Their float values are stored as exact IEEE-754 bit patterns. Each profile has its own non-inlined initialization function to bound startup stack usage. The release does not load development JSON files.

Do not commit or upload `build`, `.deps`, compiled binaries or install packages to this source-only repository. Runtime compatibility changes need both executable/layout inspection and live game tests. The guarded list of runtimes is intentional.
