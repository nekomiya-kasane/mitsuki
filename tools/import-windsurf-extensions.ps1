<#
.SYNOPSIS
  Sync Windsurf extensions into Cursor (marketplace install + copy fallback for local/unpublished).

.NOTES
  - Reads extension IDs from %USERPROFILE%\.windsurf\extensions\*\package.json
  - Skips IDs already present under %USERPROFILE%\.cursor\extensions
  - If multiple Windsurf folders for same ID, picks the lexicographically greatest folder name (usually newest).
  - stderr from `cursor` may show Node deprecation warnings; success is detected via stdout text.
#>
$ErrorActionPreference = 'Continue'
$wsRoot = Join-Path $env:USERPROFILE '.windsurf\extensions'
$crRoot = Join-Path $env:USERPROFILE '.cursor\extensions'
if (-not (Test-Path $wsRoot)) { Write-Error "Windsurf extensions not found: $wsRoot"; exit 1 }
if (-not (Test-Path $crRoot)) { New-Item -ItemType Directory -Path $crRoot -Force | Out-Null }

function Get-CursorExtensionIds {
  $set = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach ($d in (Get-ChildItem $crRoot -Directory -ErrorAction SilentlyContinue)) {
    $pj = Join-Path $d.FullName 'package.json'
    if (-not (Test-Path $pj)) { continue }
    try {
      $j = Get-Content $pj -Raw -Encoding UTF8 | ConvertFrom-Json
      if ($j.publisher -and $j.name) { [void]$set.Add("$($j.publisher).$($j.name)") }
    } catch {}
  }
  $set
}

function Get-WindsurfExtensionInfos {
  $list = [System.Collections.Generic.List[object]]::new()
  foreach ($d in (Get-ChildItem $wsRoot -Directory -ErrorAction SilentlyContinue)) {
    $pj = Join-Path $d.FullName 'package.json'
    if (-not (Test-Path $pj)) { continue }
    try {
      $j = Get-Content $pj -Raw -Encoding UTF8 | ConvertFrom-Json
      if (-not $j.publisher -or -not $j.name) { continue }
      $list.Add([PSCustomObject]@{
          Id      = "$($j.publisher).$($j.name)"
          Version = $j.version
          Folder  = $d.FullName
          DirName = $d.Name
        })
    } catch {}
  }
  $list
}

function Test-CursorInstallSuccess([string]$LogText) {
  return $LogText -match '(?i)successfully installed|is already installed|already installed'
}

$cursorIds = Get-CursorExtensionIds
$infos = @(Get-WindsurfExtensionInfos)
$byId = @{}
foreach ($i in $infos) {
  if (-not $byId.ContainsKey($i.Id)) { $byId[$i.Id] = [System.Collections.Generic.List[object]]::new() }
  $byId[$i.Id].Add($i)
}

$toProcess = [System.Collections.Generic.List[object]]::new()
foreach ($id in $byId.Keys) {
  $candidates = @($byId[$id])
  $best = $candidates | Sort-Object DirName -Descending | Select-Object -First 1
  if ($cursorIds.Contains($id)) {
    Write-Host "[skip] already in Cursor: $id" -ForegroundColor DarkGray
    continue
  }
  $toProcess.Add($best)
}

Write-Host "Windsurf unique extensions: $($byId.Count); to sync: $($toProcess.Count)" -ForegroundColor Cyan

$failed = [System.Collections.Generic.List[string]]::new()
foreach ($x in $toProcess) {
  Write-Host "`n>>> cursor --install-extension $($x.Id)" -ForegroundColor Yellow
  # Use cmd so Node stderr (deprecation) does not surface as PowerShell errors
  $log = (cmd /c "cursor --install-extension $($x.Id) 2>&1" | Out-String)
  if ($log) { Write-Host $log }

  if (Test-CursorInstallSuccess $log) {
    continue
  }

  Write-Host "    install not confirmed; copying unpacked folder..." -ForegroundColor DarkYellow
  $dest = Join-Path $crRoot $x.DirName
  if (Test-Path $dest) {
    Write-Host "    [skip copy] exists: $dest" -ForegroundColor DarkGray
    continue
  }
  try {
    Copy-Item -Path $x.Folder -Destination $dest -Recurse -Force
    Write-Host "    [ok] copied to $dest" -ForegroundColor Green
  } catch {
    Write-Host "    [fail] $($_.Exception.Message)" -ForegroundColor Red
    $failed.Add($x.Id)
  }
}

if ($failed.Count -gt 0) {
  Write-Host "`nFailures: $($failed -join ', ')" -ForegroundColor Red
  exit 1
}
Write-Host "`nDone." -ForegroundColor Green
