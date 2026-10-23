param([Parameter(Mandatory=$true)][string]$PythonPath)
$ErrorActionPreference = 'Stop'
$toolRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent $toolRoot
Push-Location $projectRoot
$previousPath = $env:PYTHONPATH
$previousExecutablePath = $env:PATH
try {
    # Unrelated tools on PATH can supply an incompatible icuuc.dll (e.g. Poppler).
    # Resolve Qt's Windows system dependencies against Windows itself.
    $env:PATH = "$(Split-Path -Parent $PythonPath);$env:WINDIR/System32;$env:WINDIR"
    $env:PYTHONPATH = "$projectRoot/build/author-tool-deps;$projectRoot/build/author-tool-deps/win32;$projectRoot/build/author-tool-deps/win32/lib;$projectRoot/build/author-tool-deps/pywin32_system32;$toolRoot"
    & $PythonPath -m PyInstaller --clean --noconfirm `
        --distpath build/author-tool-dist --workpath build/author-tool-pyinstaller `
        tools/author_tool/patcher.spec
    if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed: $LASTEXITCODE" }
} finally {
    $env:PYTHONPATH = $previousPath
    $env:PATH = $previousExecutablePath
    Pop-Location
}

