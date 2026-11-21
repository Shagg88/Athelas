# SCENES.ps1 - the scene tests: every scene of scenes.txt played on Direct3D 12 and Vulkan with the same settings,
# screenshots at fixed presented frames, and each compared with the last good set (tools/imgdiff.py) when there is
# one. TESTS ONLY: a game window opens for each run. Nothing of the player's is used: settings, saves, shader caches
# and textures are made fresh under -Out.
#
#   SCENES.ps1 -Builds C:\Athelas\build\release-out -Out C:\Athelas\work\scenes\<date>
#              [-Golden C:\Athelas\work\scenes\golden] [-Only "ta-*"] [-Renderers "0,1"] [-MakeGolden]
#
# -Builds: the folder with TT-Xbox, ROTK-Xbox, FOTR-Xbox and TA-Xbox in it. -Golden: the last good shots,
# <golden>\<scene>\r<renderer>\*.png; without it (or a scene missing there) the shots are only taken. -MakeGolden
# copies this run's shots there instead of comparing (after a person has looked at them).
# Out\summary.txt: one line a scene and renderer: shots taken, against asked for, compare result, errors in its log.
# Exit code: 0 all good, 1 something failed (a run ended early, shots missing, a comparison failed).
param([Parameter(Mandatory)][string]$Builds, [Parameter(Mandatory)][string]$Out, [string]$Golden = '',
      [string]$Only = '*', [string]$Renderers = '0,1', [switch]$MakeGolden,
      [string]$Games = 'C:\Athelas\games\xbox-us')
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exes = @{ tt = 'TT-Xbox\TwoTowers.exe'; rotk = 'ROTK-Xbox\ROTKXbox.exe'; fotr = 'FOTR-Xbox\FOTRXbox.exe'; ta = 'TA-Xbox\TAXbox.exe' }
$folders = @{ tt = 'two-towers'; rotk = 'return-of-the-king'; fotr = 'fellowship'; ta = 'third-age' }
$imgdiff = Join-Path $here '..\..\tools\imgdiff.py'
$py = Get-Command python -ErrorAction SilentlyContinue
# imgdiff needs numpy and Pillow: checked once, so a missing module reads as that and not as a picture that differs
$pyok = $false
if ($py) { $null = & python -c "import numpy, PIL" 2>&1; $pyok = $LASTEXITCODE -eq 0 }
New-Item -ItemType Directory -Force $Out | Out-Null
$summary = @(); $failed = $false
foreach ($line in Get-Content (Join-Path $here 'scenes.txt')) {
  if ($line -match '^\s*(#|$)') { continue }
  $f = $line -split '\|' | ForEach-Object { $_.Trim() }
  if ($f.Count -lt 5) { "bad line: $line"; $failed = $true; continue }
  $name, $game, $envs, $pad, $frames = $f[0], $f[1], $f[2], $f[3], $f[4]
  if ($name -notlike $Only) { continue }
  if (-not $exes.ContainsKey($game)) { "$name : no game '$game'"; $failed = $true; continue }
  $exe = Join-Path $Builds $exes[$game]
  $want = @($frames -split ',' | Where-Object { $_ }).Count
  # the run ends when every shot is there (and 3 s more); the limit is for one that never gets there
  $secs = [int](([int[]]($frames -split ',') | Measure-Object -Maximum).Maximum / 25) + 90
  foreach ($r in $Renderers -split ',') {
    $wd = Join-Path $Out "$name\r$r"
    Remove-Item $wd -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force "$wd\shots", "$wd\saves", "$wd\cache", "$wd\textures" | Out-Null
    Set-Content "$wd\settings.toml" -Encoding ascii -Value ("[display]`nmode = `"windowed`"`nwidth = 1280`nheight = 720`nvsync = false`n" +
      "[graphics]`nrenderer = `"$r`"`nwidescreen = true`nskip_logos = true`nskip_all_videos = true`nanti_aliasing = `"0`"`nsmaa = false`nupscaler = `"0`"`nframe_generation = `"off`"`n")
    Set-Content "$wd\pad.txt" -Encoding ascii -Value (($pad -split ';' | Where-Object { $_.Trim() }) -join "`n")
    $set = @('XBE_PAD_SCRIPT', 'XBE_PAD_STEPS', 'XBE_SHOT_FRAMES')
    $env:XBE_PAD_SCRIPT = "$wd\pad.txt"; $env:XBE_SHOT_FRAMES = $frames
    # only The Return of the King counts its game steps; the others' pad scripts are timed in seconds
    if ($game -eq 'rotk') { $env:XBE_PAD_STEPS = '60' }
    foreach ($kv in ($envs -split ';' | Where-Object { $_.Trim() })) { $k, $v = $kv -split '=', 2; Set-Item "Env:$($k.Trim())" $v.Trim(); $set += $k.Trim() }
    $p = Start-Process $exe -ArgumentList "--game `"$(Join-Path $Games $folders[$game])`" --config `"$wd\settings.toml`" --log `"$wd\game.log`" --saves `"$wd\saves`" --screenshots `"$wd\shots`" --shader-cache `"$wd\cache`" --textures `"$wd\textures`" --no-dialogs" -PassThru
    $full = $null
    while (((Get-Date) - $p.StartTime).TotalSeconds -lt $secs -and -not $p.HasExited) {
      Start-Sleep 1
      if (-not $full -and @(Get-ChildItem "$wd\shots" -Filter *.png -ErrorAction SilentlyContinue).Count -ge $want) { $full = Get-Date }
      if ($full -and ((Get-Date) - $full).TotalSeconds -ge 3) { break }
    }
    $early = $p.HasExited
    if (-not $p.HasExited) { $null = $p.CloseMainWindow(); $null = $p.WaitForExit(15000) }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    foreach ($k in $set) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
    Remove-Item "$wd\saves", "$wd\cache" -Recurse -Force -ErrorAction SilentlyContinue
    $got = @(Get-ChildItem "$wd\shots" -Filter *.png -ErrorAction SilentlyContinue).Count
    $errs = @(Get-Content "$wd\game.log" -ErrorAction SilentlyContinue | Select-String 'exception|crash|lost|validation|could not').Count
    $result = if ($early) { "ENDED EARLY (exit $($p.ExitCode))" } elseif ($got -ne $want) { "SHOTS $got OF $want" } else { 'ran' }
    if ($early -or $got -ne $want) { $failed = $true }
    $g = if ($Golden) { Join-Path $Golden "$name\r$r" } else { '' }
    if ($MakeGolden -and $Golden -and $got -eq $want) {
      Remove-Item $g -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $g | Out-Null
      Copy-Item "$wd\shots\*.png" $g; $result += ', made the golden set'
    } elseif ($g -and (Test-Path $g) -and $pyok) {
      $o = & python $imgdiff $g "$wd\shots" "$wd\diff" "--expect=$want" 2>&1
      $result += if ($LASTEXITCODE -eq 0) { ', matches the golden set' } else { $failed = $true; ", DIFFERS from the golden set ($($o | Select-Object -Last 1))" }
    } elseif ($g) { $result += ', no golden set to compare with' + $(if (-not $py) { ' (python not found)' } elseif (-not $pyok) { ' (python has no numpy or Pillow: NOT COMPARED)' } else { '' }) }
    $s = "{0,-14} r{1}  {2}{3}" -f $name, $r, $result, $(if ($errs) { ", $errs error lines in its log" } else { '' })
    $s; $summary += $s
  }
}
$summary | Set-Content (Join-Path $Out 'summary.txt')
if ($failed) { 'SOMETHING FAILED (see above)'; exit 1 }
'all good'
exit 0
