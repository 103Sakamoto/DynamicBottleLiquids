@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/vcvars64.bat" >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-shaders.ps1"
if errorlevel 1 exit /b 1
cd /d "%~dp0build-common"
cl @compile.rsp /c ../source/desktop/native/src/Renderer.cpp ../source/desktop/native/src/NativeOptics.cpp ../source/desktop/native/src/RegionComposer.cpp ../source/desktop/native/src/Profiles.cpp ../source/desktop/native/src/Simulation.cpp ../source/desktop/native/src/Performance.cpp ../source/desktop/native/src/GodrayShield.cpp ../source/desktop/native/src/ShieldDirect.cpp ../source/desktop/native/src/ShieldHooks.cpp
if errorlevel 1 exit /b 1
cd /d "%~dp0build-fusion"
copy /Y "..\build-common\*.obj" . >nul
cl @compile.rsp /DDBL_UI_FUSION /c ../source/desktop/native/src/Plugin.cpp ../source/desktop/native/src/Tuning.cpp ../source/desktop/native/src/FusionHost.cpp ../source/desktop/native/src/Require.cpp
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:DynamicBottleLiquids.dll Renderer.obj NativeOptics.obj RegionComposer.obj Profiles.obj Simulation.obj Performance.obj GodrayShield.obj ShieldDirect.obj ShieldHooks.obj Plugin.obj Tuning.obj FusionHost.obj Require.obj "E:/Coding/F4/HLA Bottle Shader/build/windows/x64/releasedbg/DBLFusionUI.lib" delayimp.lib /DELAYLOAD:DBLFusionUI.dll @link.rsp
if errorlevel 1 exit /b 1
cd /d "%~dp0build-mf"
copy /Y "..\build-common\*.obj" . >nul
cl @compile.rsp /DDBL_UI_MF /c ../source/desktop/native/src/Plugin.cpp ../source/desktop/native/src/Tuning.cpp ../source/desktop/native/src/OptionalUIHost.cpp
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:DynamicBottleLiquids.dll Renderer.obj NativeOptics.obj RegionComposer.obj Profiles.obj Simulation.obj Performance.obj GodrayShield.obj ShieldDirect.obj ShieldHooks.obj Plugin.obj Tuning.obj OptionalUIHost.obj @link.rsp
if errorlevel 1 exit /b 1
cd /d "%~dp0build-dmui"
copy /Y "..\build-common\*.obj" . >nul
cl @compile.rsp /DDBL_UI_DMUI /c ../source/desktop/native/src/Plugin.cpp ../source/desktop/native/src/Tuning.cpp ../source/desktop/native/src/OptionalUIHost.cpp
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:DynamicBottleLiquids.dll Renderer.obj NativeOptics.obj RegionComposer.obj Profiles.obj Simulation.obj Performance.obj GodrayShield.obj ShieldDirect.obj ShieldHooks.obj Plugin.obj Tuning.obj OptionalUIHost.obj @link.rsp
if errorlevel 1 exit /b 1
cd /d "%~dp0build-none"
copy /Y "..\build-common\*.obj" . >nul
cl @compile.rsp /DDBL_NO_UI /c ../source/desktop/native/src/Plugin.cpp ../source/desktop/native/src/Tuning.cpp ../source/desktop/native/src/OptionalUIHost.cpp
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:DynamicBottleLiquids.dll Renderer.obj NativeOptics.obj RegionComposer.obj Profiles.obj Simulation.obj Performance.obj GodrayShield.obj ShieldDirect.obj ShieldHooks.obj Plugin.obj Tuning.obj OptionalUIHost.obj @link.rsp
if errorlevel 1 exit /b 1
cd /d "%~dp0build-common"
cl /nologo /EHsc /std:c++20 /I../source/desktop/native/src ../source/desktop/native/tests/DrinkingSessionTests.cpp /Fe:DrinkingSessionTests.exe
if errorlevel 1 exit /b 1
DrinkingSessionTests.exe
if errorlevel 1 exit /b 1
cl @compile.rsp /c ../source/desktop/native/tests/StartupProfilesTests.cpp
if errorlevel 1 exit /b 1
link /nologo /OUT:StartupProfilesTests.exe StartupProfilesTests.obj Profiles.obj Simulation.obj @link.rsp
if errorlevel 1 exit /b 1
StartupProfilesTests.exe "../test-settings"
exit /b %errorlevel%

