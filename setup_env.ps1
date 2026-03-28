#!/usr/bin/env pwsh
"""
COCA Toolchain Environment Setup Script
========================================

This script sets up the complete development environment:
1. Creates virtual environment if not exists
2. Installs coca-tools (if not already installed)
3. Activates COCA toolchain environment

Usage:
    .\setup_env.ps1              # Normal setup (reuses existing installation)
    .\setup_env.ps1 -reinstall   # Force reinstall coca-tools
"""

# Parse command line arguments
$reinstall = $args -contains "-reinstall"

Write-Host "[setup] Starting COCA toolchain environment setup..." -ForegroundColor Cyan
if ($reinstall) {
    Write-Host "[setup] Force reinstall mode enabled" -ForegroundColor Yellow
}

# 1. Create virtual environment if not exists
if (-not (Test-Path "venv")) {
    Write-Host "[setup] Creating virtual environment..." -ForegroundColor Yellow
    py -m venv venv
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[error] Failed to create virtual environment" -ForegroundColor Red
        exit 1
    }
    Write-Host "[setup] Virtual environment created successfully" -ForegroundColor Green
}
else {
    Write-Host "[setup] Virtual environment already exists" -ForegroundColor Green
}

# 2. Activate virtual environment
Write-Host "[setup] Activating virtual environment..." -ForegroundColor Yellow
& "venv\Scripts\Activate.ps1"

# 3. Check if coca-tools is installed (editable install creates .egg-link or .dist-info)
$cocaToolsPath = "C:\Users\nekom\Downloads\22\COCAProjectInfra\python"
$venvSitePackages = "venv\Lib\site-packages"
$cocaInstalled = (Test-Path "$venvSitePackages\coca_tools") -or `
(Test-Path "$venvSitePackages\coca-tools.egg-link") -or `
(Get-ChildItem "$venvSitePackages" -Filter "coca_tools-*.dist-info" -ErrorAction SilentlyContinue)

# Check if coca-tools is already installed in the virtual environment
if ($cocaInstalled -and -not $reinstall) {
    Write-Host "[setup] coca-tools already installed" -ForegroundColor Green
}
else {
    if ($reinstall -and $cocaInstalled) {
        Write-Host "[setup] Reinstalling coca-tools..." -ForegroundColor Yellow
        python -m pip uninstall -y coca-tools 2>$null
    }
    else {
        Write-Host "[setup] Installing coca-tools..." -ForegroundColor Yellow
    }
    python -m pip install -e $cocaToolsPath --no-warn-script-location -q
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[error] Failed to install coca-tools" -ForegroundColor Red
        exit 1
    }
    Write-Host "[setup] coca-tools installed successfully" -ForegroundColor Green
}

# 4. Setup COCA toolchain environment
Write-Host "[setup] Configuring COCA toolchain environment..." -ForegroundColor Yellow

# Generate and execute PowerShell commands
$setupOutput = py "C:\Users\nekom\Downloads\22\toolchains\coca-toolchain\setup.py" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "[error] Failed to generate toolchain environment" -ForegroundColor Red
    exit 1
}

# Execute each line that sets an environment variable
$setupLines = $setupOutput -split "`n"
foreach ($line in $setupLines) {
    $trimmedLine = $line.Trim()
    if ($trimmedLine.StartsWith('$env:')) {
        Invoke-Expression $trimmedLine
    }
}

Write-Host "[ready] COCA toolchain environment setup complete!" -ForegroundColor Green
Write-Host "[info] Toolchain root: $env:COCA_TOOLCHAIN" -ForegroundColor Cyan

# 5. Setup Emscripten SDK environment
$emsdkPath = "C:\Users\nekom\Downloads\22\toolchains\coca-toolchain\tools\emsdk"
if (Test-Path "$emsdkPath\emsdk_env.ps1") {
    Write-Host "[setup] Activating Emscripten SDK..." -ForegroundColor Yellow
    Push-Location $emsdkPath
    & .\emsdk_env.ps1
    Pop-Location
    Write-Host "[setup] Emscripten SDK activated" -ForegroundColor Green
}
else {
    Write-Host "[warning] Emscripten SDK not found at $emsdkPath" -ForegroundColor Yellow
}

# Verify installation
Write-Host "[info] Verifying installation..." -ForegroundColor Cyan
try {
    $cmakeVersion = cmake --version 2>$null | Select-Object -First 1
    Write-Host "[info] CMake: $cmakeVersion" -ForegroundColor Gray
}
catch {
    Write-Host "[warning] CMake not found in PATH" -ForegroundColor Yellow
}

try {
    $ninjaVersion = ninja --version 2>$null
    Write-Host "[info] Ninja: $ninjaVersion" -ForegroundColor Gray
}
catch {
    Write-Host "[warning] Ninja not found in PATH" -ForegroundColor Yellow
}

try {
    $emccVersion = emcc --version 2>$null | Select-Object -First 1
    Write-Host "[info] Emscripten: $emccVersion" -ForegroundColor Gray
}
catch {
    Write-Host "[warning] emcc not found in PATH" -ForegroundColor Yellow
}

Write-Host "[info] Environment is ready for development!" -ForegroundColor Green
