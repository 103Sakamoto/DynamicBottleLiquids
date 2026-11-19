@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/vcvars64.bat" >nul
cd /d "%~dp0build-common"
cl @compile.rsp /c ../source/desktop/StaticFeedPreviewTests.cpp
if errorlevel 1 exit /b 1
link /nologo /OUT:StaticFeedPreviewTests.exe StaticFeedPreviewTests.obj @link.rsp
if errorlevel 1 exit /b 1
cd ../source/desktop
"../../build-common/StaticFeedPreviewTests.exe"
exit /b %errorlevel%
