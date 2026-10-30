# make_kit.ps1 - a port's build kit: everything a player's PC needs to build the port's program from their own copy
# of the game, and nothing made from the game.
#
# Usage, after a normal build of the port (build.bat):
#   tools\kit\make_kit.ps1 -Port C:\Athelas\source\ports\two-towers\game -Out <kit folder>
#
# The kit:
#   kit.json      the compile and link commands of the build it came from, paths relative to the kit, and every file's
#                 SHA-256
#   bin\          xbe_recomp.exe, the recompiler
#   config\       the port's config\*.toml (addresses and hooks: ours) and the SDK's xdk\ tables
#   include\      the headers the recompiled code may include: the port's src\, the SDK runtime, Dear ImGui, toml++
#   obj\          the port's own code, the SDK runtime, ImGui and the version resource, already compiled
#   lib\          SDL3 (static)
# Not in it: the recompiled code (generated\), the game, the program. Copying a .xbe, .iso or anything from generated\
# stops the script.
param(
  [Parameter(Mandatory)][string]$Port,
  [Parameter(Mandatory)][string]$Out,
  [string]$BuildDir = 'out\build\release'
)
# (an ARM64 kit: -BuildDir out\build\release-arm64. Its recompiler is the x64 build's, which Windows on ARM runs.)
$ErrorActionPreference = 'Stop'
$Port = (Resolve-Path $Port).Path.TrimEnd('\')
$bd = (Resolve-Path (Join-Path $Port $BuildDir)).Path.TrimEnd('\')
$ninja = Get-Content -LiteralPath (Join-Path $bd 'build.ninja') -Raw
# the game's program: the executable whose link takes the recompiled code (not xbe_recomp / xbe_symbols)
$exe = ''
foreach ($m in [regex]::Matches($ninja, '(?m)^build ([A-Za-z0-9_-]+)\.exe: CXX_EXECUTABLE_LINKER__[^\r\n]*')) {
  if ($m.Value -match '/generated/[^/ ]+/x86_recomp\.0\.cpp\.obj') { $exe = $m.Groups[1].Value; break }
}
if (-not $exe) { throw 'no program with recompiled code in build.ninja' }
Write-Host "kit for $exe.exe from $bd"

function Edge([string]$startsWith) {
  $i = $ninja.IndexOf("`nbuild $startsWith")
  if ($i -lt 0) { throw "no edge: build $startsWith" }
  $lines = $ninja.Substring($i + 1).Split("`n")
  $vars = @{}
  for ($k = 1; $k -lt $lines.Count; $k++) {
    $l = $lines[$k].TrimEnd("`r")
    if ($l -notmatch '^  (\w+) = (.*)$') { break }
    $vars[$matches[1]] = $matches[2]
  }
  return @{ head = $lines[0].TrimEnd("`r"); vars = $vars }
}
function Unescape([string]$p) { $p -replace '\$:', ':' -replace '\$ ', ' ' }
function Fwd([string]$p) { (Unescape $p) -replace '\\', '/' }
function IsGen([string]$p) { (Fwd $p) -match '(^|/)generated/' }

$link = Edge "$exe.exe:"
$genObj = [regex]::Match($ninja, "(?m)^build (CMakeFiles/$exe\.dir/generated/[^/]+/x86_recomp\.0\.cpp\.obj):").Groups[1].Value
if (-not $genObj) { throw 'no recompiled-code object in build.ninja' }
$comp = Edge "$($genObj):"
$genMap = [regex]::Match($ninja, "(?m)^build (CMakeFiles/$exe\.dir/generated/[^/]+/x86_func_mapping\.cpp\.obj):").Groups[1].Value
$compMap = Edge "$($genMap):"

$inputs = (($link.head -replace '^build [^:]+: \S+\s*', '') -split '\s+\|\|?\s+')[0]
$objs = @($inputs -split '\s+' | Where-Object { $_ -and -not (IsGen $_) })

if (Test-Path $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$files = [ordered]@{}
function Put([string]$from, [string]$rel) {
  if ($from -match '\.(xbe|iso)$' -or $from.StartsWith((Join-Path $Port 'generated'), [StringComparison]::OrdinalIgnoreCase) -or
      $from.StartsWith((Join-Path $Port 'assets'), [StringComparison]::OrdinalIgnoreCase)) { throw "refused (from the game): $from" }
  $to = Join-Path $Out $rel
  New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
  Copy-Item -LiteralPath $from -Destination $to -Force
  $files[($rel -replace '\\', '/')] = (Get-FileHash -Algorithm SHA256 -LiteralPath $to).Hash
}

# our compiled code
$objRel = @()
foreach ($o in $objs) {
  $src = Join-Path $bd (Unescape $o)
  $rel = 'obj/' + ((Unescape $o) -replace '^CMakeFiles/[^/]+\.dir/', '')
  Put $src $rel; $objRel += $rel
}
# libraries: our files are copied, the rest come from the Windows SDK and C runtime
$libRel = @(); $sys = @()
foreach ($t in ($link.vars['LINK_LIBRARIES'] -split '\s+' | Where-Object { $_ })) {
  $p = Fwd $t
  if ($p -match '^-l(.+)$') { $sys += ($matches[1] -replace '\.lib$', ''); continue }
  $cand = if ([IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $bd $p }
  if (-not (Test-Path -LiteralPath $cand)) { throw "library not found: $p" }
  $rel = 'lib/' + (Split-Path $cand -Leaf)
  if ($libRel -notcontains $rel) { Put $cand $rel; $libRel += $rel }
}
$sys = @($sys | Select-Object -Unique)

# compile commands of the recompiled code: defines, include folders (as kinds), flags
function Includes($vars) {
  $r = @(); $t = @($vars['INCLUDES'] -split '\s+' | Where-Object { $_ })
  for ($k = 0; $k -lt $t.Count; $k++) {
    if ($t[$k] -eq '-isystem') { $k++; $p = Fwd $t[$k]; $s = $true } elseif ($t[$k] -like '-I*') { $p = Fwd $t[$k].Substring(2); $s = $false } else { continue }
    $kind = if ($p -match '/generated/') { 'gen' } elseif ($p -match '/imgui-src$') { 'imgui' } elseif ($p -match '/tomlplusplus-src/include$') { 'toml' } elseif ($p -match '/sdk/third_party/vulkan/include$') { 'vulkan' }
            elseif ($p -eq ((Fwd $Port) + '/src')) { 'src' } elseif ($p -eq ((Fwd $Port) + '/sdk/runtime')) { 'runtime' } else { throw "unknown include folder $p" }
    $r += [ordered]@{ kind = $kind; sys = $s; from = $p }
  }
  return $r
}
$inc = Includes $comp.vars
foreach ($i in $inc) {
  $dir = $i.from -replace '/', '\'
  if ($i.kind -eq 'gen') { continue }
  Get-ChildItem -LiteralPath $dir -Recurse -File -Include *.h, *.hpp, *.inl, *.inc | ForEach-Object {
    Put $_.FullName ("include/$($i.kind)/" + $_.FullName.Substring($dir.Length + 1))
  }
}
$incOut = @($inc | ForEach-Object { [ordered]@{ kind = $_.kind; sys = $_.sys } })

# the recompiler, the port's config and the SDK's XDK tables
$recomp = Join-Path $bd 'xbe_recomp.exe'
if (-not (Test-Path $recomp)) { $recomp = Join-Path $Port 'out\build\release\xbe_recomp.exe' }
Put $recomp 'bin/xbe_recomp.exe'
Get-ChildItem -LiteralPath (Join-Path $Port 'config') -Filter *.toml | ForEach-Object { Put $_.FullName "config/$($_.Name)" }
$xdk = Join-Path $Port 'sdk\xdk'
Get-ChildItem -LiteralPath $xdk -Recurse -File -Filter *.toml | ForEach-Object { Put $_.FullName ('xdk/' + $_.FullName.Substring($xdk.Length + 1)) }

# the compiler the build used: the player's PC uses this exact version
$rules = Get-Content -LiteralPath (Get-ChildItem (Join-Path $bd 'CMakeFiles') -Filter rules.ninja -Recurse | Select-Object -First 1).FullName -Raw
$clang = ([regex]::Match($rules, "(?is)rule CXX_COMPILER__${exe}_unscanned_\w+\s+.*?command = \$\{LAUNCHER\}\$\{CODE_CHECK\}(\S+?\.exe)")).Groups[1].Value
if (-not $clang) { throw 'the C++ compiler was not found in rules.ninja' }
$clangVer = (& $clang --version | Select-Object -First 1)
$kit = [ordered]@{
  format = 1
  exe = "$exe.exe"
  arch = $(if ($clang -match 'aarch64' -or ((@($comp.vars['FLAGS'] -split '\s+') + @($comp.vars['DEFINES'] -split '\s+')) -match 'aarch64|arm64')) { 'arm64' } else { 'x64' })   # (llvm-mingw: the target is in the compiler's name)
  config = 'game.toml'
  made_from = $bd
  clang = $clangVer
  toolchain = $(if ($clang -match 'w64-mingw32') { 'llvm-mingw' } else { 'msvc' })
  msvc = $env:VCToolsVersion
  defines = @($comp.vars['DEFINES'] -split '\s+' | Where-Object { $_ })
  flags = @($comp.vars['FLAGS'] -split '\s+' | Where-Object { $_ })
  flags_map = @($compMap.vars['FLAGS'] -split '\s+' | Where-Object { $_ })
  includes = $incOut
  link_flags = @(($link.vars['FLAGS'] -split '\s+') + ($link.vars['LINK_FLAGS'] -split '\s+') | Where-Object { $_ })
  objects = $objRel
  libraries = $libRel
  system_libraries = $sys
  files = $files
}
$kit | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Out 'kit.json') -Encoding utf8
"kit ({7}): {0} files, {1:N1} MB | {2} objects, {3} libraries, {4} system libraries | {5} | MSVC {6}" -f $files.Count,
  ((Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), $objRel.Count, $libRel.Count, $sys.Count, $clangVer, $env:VCToolsVersion, $kit.arch
