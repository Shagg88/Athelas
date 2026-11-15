# RENDERER_DIFF.ps1 - one scene played twice with the same settings, by Direct3D 12 and by Vulkan, with screenshots at
# the same presented frames (XBE_SHOT_FRAMES), for tools/imgdiff.py (notes/vulkan-parity-plan.md, stage S0). TESTS
# ONLY: a game window opens for each run. Nothing of the player's is used: the game's own settings, saves, shader cache
# and texture folders are made fresh under -Out.
#
#   RENDERER_DIFF.ps1 -Exe C:\...\TAXbox.exe -Game C:\Athelas\games\xbox-us\third-age -Out C:\Athelas\work\vk\ta-title
#       [-Frames "600,900"] [-Pad "14 A"] [-Env "TA_LEVEL=e01c01;XBE_VK_VALIDATE=1"] [-Secs 40] [-Renderers "0,1"]
#       [-Settings "[textures]-table lines"] [-RenderPercent 200] [-Graphics 'smaa=true;sharpening=50']
#
# Out\r0\shots, Out\r1\shots: the PNGs; Out\r0\game.log, Out\r1\game.log; Out\run.txt: what was run.
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$Game, [Parameter(Mandatory)][string]$Out,
      [string]$Frames = '600,900', [string]$Pad = '', [string]$Env = '', [int]$Secs = 40, [string]$Renderers = '0,1',
      [string]$Settings = '', [string]$RenderPercent = '100', [string]$Graphics = '')
$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force $Out | Out-Null
"exe $Exe`ngame $Game`nframes $Frames`npad $($Pad -replace "`n", ' | ')`nenv $Env`nsecs $Secs`n$(Get-Date -Format o)" | Set-Content "$Out\run.txt"
foreach ($r in ($Renderers -split ',')) {
  $w = "$Out\r$r"
  Remove-Item $w -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force "$w\shots", "$w\saves", "$w\cache", "$w\textures" | Out-Null
  # the comparison settings (plan section 4): everything that differs between renderers off, a fixed window
  # -Graphics "smaa=true;sharpening=50": [graphics] keys over the comparison values (TOML values as written)
  $gfx = [ordered]@{ renderer = "`"$r`""; anti_aliasing = '"0"'; smaa = 'false'; upscaler = '"0"'; frame_generation = '"off"';
    sharpening = '0'; colour_look = '"original"'; brightness = '1.00'; hdr = 'false'; anisotropic_filtering = '"16"';
    lod_bias = '0.00'; skip_logos = 'true' }
  foreach ($kv in ($Graphics -split ';' | Where-Object { $_ })) { $k, $v = $kv -split '=', 2; $gfx[$k.Trim()] = $v.Trim() }
  $gtext = ($gfx.Keys | ForEach-Object { "$_ = $($gfx[$_])" }) -join "`n"
  Set-Content "$w\settings.toml" -Encoding ascii -Value ("[display]`nmode = `"windowed`"`nwidth = 1280`nheight = 960`nvsync = false`nrender_percent = `"$RenderPercent`"`n" +
    "[graphics]`n$gtext`n" + "[textures]`nreplace = false`n" + $Settings)
  if ($Pad) { Set-Content "$w\pad.txt" -Encoding ascii -Value $Pad; $env:XBE_PAD_SCRIPT = "$w\pad.txt" }
  $env:XBE_SHOT_FRAMES = $Frames
  $set = @()
  foreach ($kv in ($Env -split ';' | Where-Object { $_ })) { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v; $set += $k }
  $p = Start-Process $Exe -ArgumentList "--game `"$Game`" --config `"$w\settings.toml`" --log `"$w\game.log`" --saves `"$w\saves`" --screenshots `"$w\shots`" --textures `"$w\textures`" --shader-cache `"$w\cache`" --no-dialogs" -PassThru
  while (((Get-Date) - $p.StartTime).TotalSeconds -lt $Secs -and -not $p.HasExited) { Start-Sleep 1 }
  $ended = $p.HasExited
  if (-not $p.HasExited) { $null = $p.CloseMainWindow(); $null = $p.WaitForExit(10000) }
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
  Remove-Item Env:XBE_PAD_SCRIPT, Env:XBE_SHOT_FRAMES -ErrorAction SilentlyContinue
  foreach ($k in $set) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
  $shots = @(Get-ChildItem "$w\shots" -Filter *.png -ErrorAction SilentlyContinue)
  "renderer $r : $($shots.Count) shots$(if ($ended) { ', the game ended by itself (exit ' + $p.ExitCode + ')' })"
  if (Test-Path "$w\game.log") {
    Get-Content "$w\game.log" | Select-String 'vulkan:|d3d12: not used|renderer|test: screenshot|validation|error' | Select-Object -First 40 | ForEach-Object { "  " + $_.Line }
  }
}
