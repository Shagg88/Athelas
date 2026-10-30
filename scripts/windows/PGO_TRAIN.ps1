# PGO_TRAIN.ps1 - a port's profile-guided build, trained and measured (GAMEPLAY: the GPU and screen are used).
# 1. the measuring build (out\build\pgo-gen, XBE_PGO=GEN) plays a scripted route; 2. its counts are merged into
#    ports\<Port>\pgo\<Exe>.profdata; 3. the normal mingw build is rebuilt with it; 4. the same route is played by the
#    build without the profile and the one with it, and their fps lines are printed side by side.
# Usage: PGO_TRAIN.ps1 -Port two-towers\game -Exe TwoTowers -Game two-towers [-Train 240] [-Measure 150] [-Pad "10 A`n14 A"]
param([string]$Port = 'third-age\taxbox', [string]$Exe = 'TAXbox', [string]$Game = 'third-age', [int]$Train = 240, [int]$Measure = 150,
      [string]$Pad = "16 A`n22 A`n26 A`n30 A`n50 A`n54 A`n58 A")
$ErrorActionPreference = 'Continue'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$cm = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"
$env:PATH = "$cm\CMake\bin;$cm\Ninja;C:\Athelas\tools\llvm-mingw\bin;$env:PATH"
$d = "C:\Athelas\source\ports\$Port"; $game = "C:\Athelas\games\xbox-us\$Game"
$p = "C:\Athelas\work\pgo-$Exe"; Remove-Item $p -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $p | Out-Null
Copy-Item "$d\out\build\mingw\$Exe.exe" "$p\$Exe-plain.exe"
function Run([string]$exe, [string]$name, [int]$secs) {
  $w = "$p\$name"; New-Item -ItemType Directory -Force "$w\shots" | Out-Null
  Set-Content "$w\settings.toml" -Encoding ascii -Value "[display]`nwidth = 2560`nheight = 1440`nshow_fps = true`nvsync = false`n[graphics]`nwidescreen = true`nfps = `"0`"`nsharp_text = true`nskip_logos = true`nsecond_core = true`nanti_aliasing = `"4`"`nalpha_edges = true`nsmaa = true`nanisotropic_filtering = `"16`""
  Set-Content "$w\pad.txt" -Encoding ascii -Value $Pad
  $env:XBE_PAD_SCRIPT = "$w\pad.txt"
  $pr = Start-Process $exe -ArgumentList "--game `"$game`" --config `"$w\settings.toml`" --log `"$w\game.log`" --saves `"$w\saves`" --screenshots `"$w\shots`" --textures `"$w\textures`"" -PassThru
  while (((Get-Date) - $pr.StartTime).TotalSeconds -lt $secs -and -not $pr.HasExited) { Start-Sleep 1 }
  if (-not $pr.HasExited) { $null = $pr.CloseMainWindow(); $null = $pr.WaitForExit(15000) }
  if (-not $pr.HasExited) { Stop-Process -Id $pr.Id -Force; "$name killed" }
  Remove-Item Env:XBE_PAD_SCRIPT
}
"== 1. training"
$env:LLVM_PROFILE_FILE = "$p\run-%p.profraw"
Run "$d\out\build\pgo-gen\$Exe.exe" 'train' $Train
Remove-Item Env:LLVM_PROFILE_FILE
Get-ChildItem $p -Filter *.profraw | Select-Object Name, Length
"== 2. merge"
New-Item -ItemType Directory -Force "$d\pgo" | Out-Null
llvm-profdata merge -o "$d\pgo\$Exe.profdata" (Get-ChildItem $p -Filter *.profraw).FullName 2>&1
llvm-profdata show "$d\pgo\$Exe.profdata" 2>&1 | Select-Object -Last 4
"== 3. build with the profile"
Push-Location $d; cmake --preset mingw 2>&1 | Select-String 'PGO'
$t = Get-Date; cmake --build out\build\mingw --target $Exe 2>&1 | Select-String ' error|FAILED|undefined|profile' | Select-Object -First 10; "exit $LASTEXITCODE in $([int]((Get-Date)-$t).TotalMinutes) min"; Pop-Location
"== 4. compare (same route, 150 s each)"
Run "$p\$Exe-plain.exe" 'plain' $Measure
Run "$d\out\build\mingw\$Exe.exe" 'pgo' $Measure
foreach ($n in 'plain','pgo') { "-- $n"; Get-Content "$p\$n\game.log" | Select-String 'fps:' | ForEach-Object { $_.Line.Substring(0, [Math]::Min(70, $_.Line.Length)) } }
