param(
    [Parameter(Mandatory = $true)][Alias('i')][string]$InputPdf,
    [Parameter(Mandatory = $false)][Alias('o')][string]$OutputPdf,
    [Parameter(Mandatory = $false)][Alias('d')][int]$Dpi = 600
)

$ErrorActionPreference = 'Stop'

# Resolve paths
$InputPdf = Resolve-Path $InputPdf
if (-not $OutputPdf) {
    $dir = [System.IO.Path]::GetDirectoryName($InputPdf)
    $name = [System.IO.Path]::GetFileNameWithoutExtension($InputPdf)
    $OutputPdf = Join-Path $dir "${name}_img.pdf"
}

# Find magick.exe (handle WindowsApps symlink)
$magickPath = Get-Command magick -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $magickPath -or -not (Test-Path $magickPath)) {
    $windowsAppsMagick = Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps\magick.exe"
    if (Test-Path $windowsAppsMagick) {
        $magickPath = $windowsAppsMagick
    }
    else {
        Write-Error "Required command 'magick' not found. Install ImageMagick via winget: winget install --id ImageMagick.Q16-HDRI"
        exit 1
    }
}

# Find gswin64c.exe (Ghostscript)
$gsPath = Get-Command gswin64c -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $gsPath -or -not (Test-Path $gsPath)) {
    # Try default installation path
    $defaultGsPath = "C:\Program Files\gs\gs10.03.1\bin\gswin64c.exe"
    if (Test-Path $defaultGsPath) {
        $gsPath = $defaultGsPath
    }
    else {
        Write-Error "Required command 'gswin64c' not found. Ghostscript should be installed."
        exit 1
    }
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) "to_img_pdf_$(Get-Random)"
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

try {
    Write-Host "Converting PDF pages to images at ${Dpi} DPI ..."
    # Ghostscript: PDF -> PNG per page
    & $gsPath -dNOPAUSE -dBATCH -dSAFER -sDEVICE=png16m "-r$Dpi" "-sOutputFile=$tmpDir\page_%04d.png" "$InputPdf"
    if ($LASTEXITCODE -ne 0) { throw "Ghostscript failed (exit $LASTEXITCODE)" }

    $pages = Get-ChildItem "$tmpDir\page*.png" | Sort-Object Name
    if ($pages.Count -eq 0) { throw "No pages rendered" }
    Write-Host "Rendered $($pages.Count) page(s). Assembling image PDF ..."

    # ImageMagick: PNGs -> single PDF (each page is a raster image)
    $pageFiles = $pages | ForEach-Object { $_.FullName }
    & $magickPath @pageFiles -density $Dpi -units PixelsPerInch "$OutputPdf"
    if ($LASTEXITCODE -ne 0) { throw "ImageMagick failed (exit $LASTEXITCODE)" }

    Write-Host "Done: $OutputPdf"
}
finally {
    Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
}
