@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/vcvars64.bat" >nul
cd /d "%~dp0build-common"
cl /nologo /EHsc /std:c++20 /I../source/desktop/native/src ../source/desktop/native/tests/AcidSoakerLevelTests.cpp /Fe:AcidSoakerLevelTests.exe
if errorlevel 1 exit /b 1
AcidSoakerLevelTests.exe
if errorlevel 1 exit /b 1
cl @compile.rsp /c ../source/desktop/native/tests/AcidSoakerBubbleTests.cpp
if errorlevel 1 exit /b 1
link /nologo /OUT:AcidSoakerBubbleTests.exe AcidSoakerBubbleTests.obj Profiles.obj Simulation.obj @link.rsp
if errorlevel 1 exit /b 1
AcidSoakerBubbleTests.exe ../test-settings
if errorlevel 1 exit /b 1
cl @compile.rsp /c ../source/desktop/ShieldTests.cpp ../source/desktop/ReflectionSettingsTests.cpp ../source/desktop/GlassTests.cpp ../source/desktop/SceneBubbleBudgetTests.cpp
if errorlevel 1 exit /b 1
link /nologo /OUT:ShieldTests.exe ShieldTests.obj GodrayShield.obj Simulation.obj @link.rsp
if errorlevel 1 exit /b 1
ShieldTests.exe
if errorlevel 1 exit /b 1
link /nologo /OUT:ReflectionSettingsTests.exe ReflectionSettingsTests.obj Profiles.obj Simulation.obj @link.rsp
if errorlevel 1 exit /b 1
ReflectionSettingsTests.exe ../test-settings
if errorlevel 1 exit /b 1
link /nologo /OUT:SceneBubbleBudgetTests.exe SceneBubbleBudgetTests.obj Simulation.obj @link.rsp
if errorlevel 1 exit /b 1
SceneBubbleBudgetTests.exe
if errorlevel 1 exit /b 1
link /nologo /OUT:GlassTests.exe GlassTests.obj @link.rsp
if errorlevel 1 exit /b 1
cd ../source/desktop
"../../build-common/GlassTests.exe"
exit /b %errorlevel%

