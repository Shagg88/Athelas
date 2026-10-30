# kit_build.ps1 - builds a port's program from its build kit (make_kit.ps1) and the player's own default.xbe: what
# the launcher will do on a player's PC, as a script to prove and time it. No CMake or Ninja: the recompiler, clang on
# each recompiled file (as many at a time as the processor and free memory allow), then one link.
#
# Usage: tools\kit\kit_build.ps1 -Kit <kit> -Xbe <default.xbe> -Out <folder> [-Jobs N]
# The compiler and the Windows SDK / C runtime: the Visual Studio on this PC for now (the launcher will bring its own
# pinned clang and Microsoft's libraries when there is none).
param(
  [Parameter(Mandatory)][string]$Kit,
  [Parameter(Mandatory)][string]$Xbe,
  [Parameter(Mandatory)][string]$Out,
  [int]$Jobs = 0,
  [string]$Tools = 'C:\Athelas\tools\llvm-mingw'   # a llvm-mingw kit: the llvm-mingw Athelas ships
)
# (Continue: Windows PowerShell turns a program's stderr line into a stopping error under Stop; each step checks its
# exit code instead)
$ErrorActionPreference = 'Continue'
$t0 = Get-Date
function Phase($what) { Write-Host ("[{0,5:N0} s] {1}" -f ((Get-Date) - $t0).TotalSeconds, $what) }
function Fail($what) { Write-Host "KIT BUILD FAILED: $what"; exit 1 }
$Kit = (Resolve-Path $Kit).Path
$k = Get-Content -LiteralPath (Join-Path $Kit 'kit.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$work = Join-Path $Out 'build'
if (Test-Path $work) { Remove-Item -LiteralPath $work -Recurse -Force }
$gen = Join-Path $work 'generated'; $objDir = Join-Path $work 'obj'
New-Item -ItemType Directory -Force $gen, $objDir | Out-Null

# ---- the toolchain
$arch = if ($k.arch) { $k.arch } else { 'x64' }
if ($k.toolchain -eq 'llvm-mingw') {
  $clangxx = Join-Path $Tools ("bin\{0}-w64-mingw32-clang++.exe" -f $(if ($arch -eq 'arm64') { 'aarch64' } else { 'x86_64' }))
  if (-not (Test-Path $clangxx)) { Fail "no $clangxx" }
  $ver = & $clangxx --version | Select-Object -First 1
  if ($ver -ne $k.clang) { Fail "clang: the kit needs '$($k.clang)', the tools have '$ver'" }
} else {
  $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
  # the kit's target (x64 or arm64) from this PC's own processor: Visual Studio's developer environment for that pair
  $arch = if ($k.arch) { $k.arch } else { 'x64' }
  $hostArm = $env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64'
  $tArch = if ($arch -eq 'arm64') { 'arm64' } else { 'amd64' }; $hArch = if ($hostArm) { 'arm64' } else { 'amd64' }
  cmd /c "`"$vs\Common7\Tools\VsDevCmd.bat`" -arch=$tArch -host_arch=$hArch >nul && set" | ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] } }
  $clangxx = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang++.exe'
  if ($hostArm -and (Test-Path (Join-Path $vs 'VC\Tools\Llvm\ARM64\bin\clang++.exe'))) { $clangxx = Join-Path $vs 'VC\Tools\Llvm\ARM64\bin\clang++.exe' }
  if ($arch -eq 'arm64' -and -not (Test-Path (Join-Path $env:VCToolsInstallDir 'lib\arm64\libcmt.lib'))) { Fail "Visual Studio's ARM64 libraries are missing (MSVC Build Tools for ARM64)" }
  $ver = & $clangxx --version | Select-Object -First 1
  if ($ver -ne $k.clang) { Fail "clang: the kit needs '$($k.clang)', this PC has '$ver'" }
  if ($env:VCToolsVersion -ne $k.msvc) { Write-Host "note: MSVC $env:VCToolsVersion here, the kit was made with $($k.msvc)" }
}
$cpu = (Get-CimInstance Win32_Processor | Measure-Object NumberOfLogicalProcessors -Sum).Sum
$freeGB = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
# at least 3/4 of the threads; all of them when free memory allows (job 012: 5 compiles took ~1 GB in all, 0.5 GB each is safe)
if ($Jobs -le 0) { $Jobs = [Math]::Max([int][Math]::Ceiling($cpu * 0.75), [Math]::Min($cpu, [int][Math]::Floor(($freeGB - 2) / 0.5))) }
Phase ("{0} threads, {1:N1} GB free: {2} compiles at a time" -f $cpu, $freeGB, $Jobs)

# ---- 1. recompile the player's default.xbe with the kit's config (its xbe path and output folder pointed here)
$cfgDir = Join-Path $work 'config'
New-Item -ItemType Directory -Force $cfgDir | Out-Null
Copy-Item (Join-Path $Kit 'config\*.toml') $cfgDir
$cfg = Join-Path $cfgDir $k.config
$c = Get-Content -LiteralPath $cfg -Raw
$c = [regex]::Replace($c, '(?m)^path\s*=\s*"[^"]*"', ('path = "' + ((Resolve-Path $Xbe).Path -replace '\\', '/') + '"'))
$c = [regex]::Replace($c, '(?m)^out\s*=\s*"[^"]*"', ('out = "' + ($gen -replace '\\', '/') + '"'))
Set-Content -LiteralPath $cfg -Value $c -Encoding utf8
Phase 'recompiling the game code'
Push-Location $work
& (Join-Path $Kit 'bin\xbe_recomp.exe') $cfg --xdk (Join-Path $Kit 'xdk') 2>&1 | Select-Object -Last 3 | ForEach-Object { Write-Host "  recomp: $_" }
$rc = $LASTEXITCODE
Pop-Location
if ($rc -ne 0) { Fail "the recompiler stopped ($rc)" }
$srcs = @(Get-ChildItem -LiteralPath $gen -Filter *.cpp | Sort-Object Length -Descending)
if (-not $srcs.Count) { Fail 'the recompiler wrote no code' }
Phase ("{0} files, {1:N0} MB of C++" -f $srcs.Count, (($srcs | Measure-Object Length -Sum).Sum / 1MB))

# ---- 2. compile
$inc = @()
foreach ($i in $k.includes) {
  $p = if ($i.kind -eq 'gen') { $gen } else { Join-Path $Kit "include\$($i.kind)" }
  if ($i.sys) { $inc += '-isystem'; $inc += $p } else { $inc += "-I$p" }
}
# arguments go to clang as one command line: a path with a space is put in quotes; a define keeps the quoting build.ninja
# gave it (-DGAME_VERSION=\"1.6.0\")
function Quote($a) { $a | ForEach-Object { if ($_ -match '\s' -and $_ -notmatch '"') { '"' + $_ + '"' } else { $_ } } }
$queue = New-Object System.Collections.Queue; $srcs | ForEach-Object { $queue.Enqueue($_) }
$running = @(); $done = 0; $failed = @(); $minFree = [double]::MaxValue; $shown = -1
while ($queue.Count -or $running.Count) {
  while ($running.Count -lt $Jobs -and $queue.Count) {
    $s = $queue.Dequeue()
    $flags = if ($s.Name -eq 'x86_func_mapping.cpp') { $k.flags_map } else { $k.flags }
    $cargs = @($k.defines) + $inc + @($flags) + @('-o', (Join-Path $objDir ($s.Name + '.obj')), '-c', $s.FullName)
    $p = Start-Process -FilePath $clangxx -ArgumentList (Quote $cargs) -NoNewWindow -PassThru -RedirectStandardError (Join-Path $objDir ($s.Name + '.err'))
    $null = $p.Handle   # (without this, PowerShell never reads the exit code: every file would look failed)
    $running += @{ p = $p; s = $s }
  }
  Start-Sleep -Milliseconds 500
  $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
  if ($free -lt $minFree) { $minFree = $free }
  $still = @()
  foreach ($r in $running) { if ($r.p.HasExited) { $done++; $r.p.WaitForExit(); if ($r.p.ExitCode -ne 0 -or -not (Test-Path (Join-Path $objDir ($r.s.Name + '.obj')))) { $failed += $r.s.Name } } else { $still += $r } }
  $running = $still
  $pct = [int][Math]::Floor(100 * $done / $srcs.Count / 10) * 10
  if ($pct -ne $shown) { Phase "  compiled $pct%"; $shown = $pct }
}
if ($failed.Count) {
  foreach ($f in $failed | Select-Object -First 3) { Get-Content (Join-Path $objDir ($f + '.err')) -TotalCount 12 | ForEach-Object { Write-Host "  $f | $_" } }
  Fail "$($failed.Count) files didn't compile"
}

# ---- 3. link: the recompiled code, the kit's objects and libraries, the Windows / C runtime libraries
Phase 'linking'
$exe = Join-Path $Out $(if ($arch -eq 'arm64') { $k.exe -replace '\.exe$', '-arm64.exe' } else { $k.exe })
$objs = @(Get-ChildItem -LiteralPath $objDir -Filter *.obj | ForEach-Object FullName) + @($k.objects | ForEach-Object { Join-Path $Kit $_ })
$libs = @($k.libraries | ForEach-Object { Join-Path $Kit $_ }) + @($k.system_libraries | ForEach-Object { "-l$_" })
$rsp = Join-Path $work 'link.rsp'
(Quote ((@($k.link_flags) + @('-o', $exe) + $objs + $libs) -replace '\\', '/')) | Set-Content -LiteralPath $rsp -Encoding ascii
& $clangxx "@$rsp" 2>&1 | Select-Object -Last 10 | ForEach-Object { Write-Host "  link: $_" }
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) { Fail 'the link failed' }
Phase ("built {0} ({1:N1} MB) | lowest free memory {2:N1} GB" -f $exe, ((Get-Item $exe).Length / 1MB), $minFree)
