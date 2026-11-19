@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/vcvars64.bat" >nul
cd /d "%~dp0build-common"
cl @compile.rsp /c ../SurfaceTests.cpp ../ViewTests.cpp
if errorlevel 1 exit /b 1
link /nologo /OUT:SurfaceTests.exe SurfaceTests.obj Profiles.obj Simulation.obj @link.rsp
if errorlevel 1 exit /b 1
SurfaceTests.exe "../test-settings"
if errorlevel 1 exit /b 1
link /nologo /OUT:ViewTests.exe ViewTests.obj NativeOptics.obj RegionComposer.obj Performance.obj Simulation.obj @link.rsp
if errorlevel 1 exit /b 1
ViewTests.exe
exit /b %errorlevel%

