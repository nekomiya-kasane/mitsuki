# Runs COCA setup.py (Invoke-Expression), then forwards all arguments to cmake.
# Invoked by cmake-with-coca.cmd; CMake Tools sets cmake.cmakePath to that .cmd.

$ErrorActionPreference = 'Stop'

$cocaRoot = $env:COCA_TOOLCHAIN_ROOT
if (-not $cocaRoot) { $cocaRoot = $env:COCA_TOOLCHAIN }

if (-not $cocaRoot) {
    Write-Error @"
COCA toolchain path not set. Set COCA_TOOLCHAIN_ROOT (folder containing setup.py).
CMake Tools: add to .vscode/settings.json -> cmake.environment.COCA_TOOLCHAIN_ROOT
"@
    exit 1
}

$setupPy = Join-Path $cocaRoot 'setup.py'
if (-not (Test-Path -LiteralPath $setupPy)) {
    Write-Error "setup.py not found: $setupPy"
    exit 1
}

python $setupPy | Invoke-Expression

$cmakeExe = $env:CMAKE_REAL
if (-not $cmakeExe) {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) {
        $cmakeExe = $cmd.Source
    }
}

if (-not $cmakeExe) {
    Write-Error 'cmake not found after COCA activation. Check setup.py PATH.'
    exit 1
}

& $cmakeExe @args
exit $LASTEXITCODE
