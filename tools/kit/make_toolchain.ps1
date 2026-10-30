# make_toolchain.ps1 - the build tools Athelas ships (tools\llvm-mingw in the release): llvm-mingw (github.com/mstorsjo/
# llvm-mingw) trimmed to what the launcher's builder runs, x64 and ARM64 targets only. clang and lld are Apache-2.0 with
# the LLVM exception, the mingw-w64 headers and runtime are public domain / permissive, libc++ and libunwind are
# Apache-2.0: all of it may be redistributed. Nothing of Microsoft's SDK is in it.
# One per zip: the x64 zip's (x64 tools making x64 programs) and the ARM64 zip's (llvm-mingw's ARM64-hosted release,
# making ARM64 programs, so a Windows on ARM PC builds at full speed).
#
# Usage: tools\kit\make_toolchain.ps1 -From <unpacked llvm-mingw for that PC> -Target x86_64|aarch64 -Out <folder>
# It proves the copy by compiling and linking a small Windows program with it (and running it when this PC can).
param(
  [Parameter(Mandatory)][string]$From,
  [Parameter(Mandatory)][ValidateSet('x86_64', 'aarch64')][string]$Target,
  [Parameter(Mandatory)][string]$Out
)
$ErrorActionPreference = 'Stop'
$From = (Resolve-Path $From).Path.TrimEnd('\')
if (Test-Path $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path.TrimEnd('\')
function CopyTree($rel) { $s = Join-Path $From $rel; if (Test-Path $s) { $d = Join-Path $Out $rel; New-Item -ItemType Directory -Force (Split-Path $d) | Out-Null; Copy-Item -LiteralPath $s $d -Recurse -Force } }

# bin: the compiler (clang-NN.exe with its two DLLs), the per-target wrappers and their settings, the linker, the
# runtime DLLs these tools need, llvm-profdata (profile-guided builds)
New-Item -ItemType Directory -Force "$Out\bin" | Out-Null
$keep = @('clang-target-wrapper.exe', 'clang.exe', 'clang++.exe', 'ld.lld.exe', 'lld.exe', 'llvm-profdata.exe',
          'libLLVM-*.dll', 'libclang-cpp.dll', 'libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll', 'libffi-8.dll',
          'clang-??.exe', 'mingw32-common.cfg')
$keep += "$Target-w64-mingw32-clang.exe", "$Target-w64-mingw32-clang++.exe", "$Target-w64-mingw32-ld", "$Target-*windows-gnu.cfg"
foreach ($p in $keep) {
  Get-ChildItem -Path "$From\bin" -Filter $p -File | Where-Object { $_.Name -notmatch '(armv7|i686|arm64ec|uwp)' -and ($_.Name -notmatch '^(x86_64|aarch64)-' -or $_.Name -like "$Target-*") } | ForEach-Object { Copy-Item $_.FullName "$Out\bin\" }
}
# headers (shared by both targets), each target's libraries, clang's own headers and runtime pieces for Windows
CopyTree 'include'
CopyTree "$Target-w64-mingw32"
foreach ($c in Get-ChildItem "$From\lib\clang" -Directory) {
  CopyTree "lib\clang\$($c.Name)\include"
  New-Item -ItemType Directory -Force "$Out\lib\clang\$($c.Name)\lib\windows" | Out-Null
  Get-ChildItem "$From\lib\clang\$($c.Name)\lib\windows" -File | Where-Object { $_.Name -match $Target -and $_.Name -notmatch 'asan|dynamic|fuzzer|xray|hwasan|memprof|tsan|msan' } |
    ForEach-Object { Copy-Item $_.FullName "$Out\lib\clang\$($c.Name)\lib\windows\" }
}
# the licences
New-Item -ItemType Directory -Force "$Out\licenses" | Out-Null
Get-ChildItem $From -File | Where-Object { $_.Name -match 'LICENSE|README|COPYING' } | ForEach-Object { Copy-Item $_.FullName "$Out\licenses\" }
@"
llvm-mingw, trimmed for Athelas ($Target): github.com/mstorsjo/llvm-mingw
LLVM, clang, lld, libc++, libunwind: Apache License 2.0 with LLVM Exceptions (llvm.org/LICENSE.txt)
mingw-w64 headers and runtime: public domain / ZPL / MIT-style (mingw-w64.org, COPYING in its source)
The source of every part is at the addresses above.
"@ | Set-Content "$Out\licenses\ATHELAS-NOTE.txt" -Encoding ascii

# prove it: a small Windows program for both targets, with C++ and the libraries the ports use
$t = Join-Path $env:TEMP 'athelas-tc-test'; New-Item -ItemType Directory -Force $t | Out-Null
@'
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <xaudio2.h>
#include <string>
#include <vector>
#include <thread>
#include <filesystem>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::vector<std::string> v{"a", "b"};
    std::thread th([&] { v.push_back("c"); }); th.join();
    IDXGIFactory4 *f = nullptr; CreateDXGIFactory2(0, IID_PPV_ARGS(&f)); if (f) f->Release();
    return (int)v.size() - 3 + (std::filesystem::exists(L"C:\\") ? 0 : 1);
}
'@ | Set-Content "$t\t.cpp" -Encoding ascii
$hostArm = $env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64'
if ($Target -eq 'aarch64' -and -not $hostArm) {   # (the ARM64 zip's tools are ARM64 programs: an x64 PC can't run them)
  "toolchain ($Target): {0:N0} MB, {1} files; not tried (an x64 PC can't run ARM64 tools)" -f ((Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), (Get-ChildItem $Out -Recurse -File).Count
  Remove-Item $t -Recurse -Force; return
}
& "$Out\bin\$Target-w64-mingw32-clang++.exe" -std=gnu++20 -O2 -static -municode -mwindows "$t\t.cpp" -o "$t\t.exe" -ld3d12 -ldxgi -ldxguid -lxaudio2_9 2>&1 | Select-Object -First 10
if ($LASTEXITCODE -ne 0 -or -not (Test-Path "$t\t.exe")) { throw "the trimmed tools can't build for $Target" }
$ran = ''
if ($hostArm -eq ($Target -eq 'aarch64')) {
  $p = Start-Process "$t\t.exe" -PassThru -Wait
  if ($p.ExitCode -ne 0) { throw "the test program returned $($p.ExitCode)" }
  $ran = ' and ran'
}
Remove-Item $t -Recurse -Force
$mb = (Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
"toolchain ($Target): {0:N0} MB, {1} files; built$ran a test program" -f $mb, (Get-ChildItem $Out -Recurse -File).Count
