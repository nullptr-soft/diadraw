# Makes DiaDraw.msix from an installed build. From the root, after `cmake --build --preset windows`:
#   powershell -ExecutionPolicy Bypass -File .\src\apps\winui\msix\pack.ps1 [-Pfx cert.pfx -Password <pw>]
# Without -Pfx the package is left unsigned (Windows installs only signed packages; for a local
# test, make a certificate whose subject matches DIADRAW_MSIX_PUBLISHER, see README.md).
param(
  [string]$Build = (Join-Path $PSScriptRoot '..\..\..\..\..\diadraw-build\windows'),
  [string]$Pfx,
  [string]$Password
)
$ErrorActionPreference = 'Stop'
$stage = Join-Path $Build 'msix-stage'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
cmake --install $Build --prefix $stage --component app
New-Item -ItemType Directory (Join-Path $stage 'Assets') | Out-Null
Copy-Item (Join-Path $PSScriptRoot '..\..\icons\msix\*.png') (Join-Path $stage 'Assets')
Copy-Item (Join-Path $Build 'apps\winui\AppxManifest.xml') $stage
# The Windows SDK's makeappx and signtool (on PATH in a VS developer prompt).
$out = Join-Path $Build 'DiaDraw.msix'
makeappx pack /o /d $stage /p $out
if ($Pfx) { signtool sign /fd SHA256 /f $Pfx /p $Password $out }
Write-Output "made $out"
