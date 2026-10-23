param([string]$PythonPath)
$ErrorActionPreference = 'Stop'
$toolRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent $toolRoot
$localPython = Join-Path $projectRoot 'build/author-tool-venv/Scripts/python.exe'
if (-not $PythonPath) {
    if (Test-Path -LiteralPath $localPython) { $PythonPath = $localPython }
    else {
        $bundledPython = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
        if ((Test-Path -LiteralPath $bundledPython) -and (Test-Path (Join-Path $projectRoot 'build/author-tool-deps'))) { $PythonPath = $bundledPython }
        else { throw 'Create build/author-tool-venv as described in README.md, or pass -PythonPath with a Python executable that has the requirements installed.' }
    }
}
$priorPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = $toolRoot
    $workspaceDeps = Join-Path $projectRoot 'build/author-tool-deps'
    if (Test-Path -LiteralPath $workspaceDeps) { $env:PYTHONPATH = "$workspaceDeps;$workspaceDeps/win32;$workspaceDeps/win32/lib;$workspaceDeps/pywin32_system32;$toolRoot" }
    & $PythonPath -m author_tool
    if ($LASTEXITCODE -ne 0) { throw "Author tool exited with code $LASTEXITCODE" }
} finally { $env:PYTHONPATH = $priorPythonPath }
