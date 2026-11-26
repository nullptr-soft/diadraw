# Windows version of run.sh: runs a gesture script against DiaDraw.exe and compares its log with
# the expected one. usage: run.ps1 <path to DiaDraw.exe> <name>
param([string]$App, [string]$Name)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$work = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory $work | Out-Null
try {
  Copy-Item (Join-Path $here 'gestures.draw') (Join-Path $work 'test.draw')
  Copy-Item (Join-Path $here "$Name.script") (Join-Path $work "$Name.script")
  $env:DIADRAW_SCRIPT = Join-Path $work "$Name.script"
  # A WIN32 app does not block the console: wait for it.
  Start-Process -FilePath $App -ArgumentList "`"$(Join-Path $work 'test.draw')`"" -Wait
  $expected = (Get-Content -Raw (Join-Path $here "$Name.expected")) -replace "`r`n", "`n"
  $actual = (Get-Content -Raw (Join-Path $work "$Name.script.log")) -replace "`r`n", "`n"
  if ($expected -ne $actual) {
    Write-Output "--- expected"; Write-Output $expected
    Write-Output "--- actual"; Write-Output $actual
    exit 1
  }
} finally {
  Remove-Item -Recurse -Force $work
}
