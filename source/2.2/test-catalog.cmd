@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/vcvars64.bat" >nul
cd /d "%~dp0build-common"
cl @compile.rsp /c ../CatalogTests.cpp
if errorlevel 1 exit /b 1
link /nologo /OUT:CatalogTests.exe CatalogTests.obj Profiles.obj Simulation.obj @link.rsp
exit /b %errorlevel%
