; Athelas-Setup.exe: three steps (Shagg: simple) - pick the folder, install, finish with "Start Athelas" ticked.
; Adds Start menu and desktop shortcuts. Built by scripts/installer/make_setup.sh from the release's files.
; No admin rights: Athelas writes its settings beside Athelas.exe, so it must live in a folder the player owns.
; The uninstaller removes only the files the download put there (from athelas-files.txt at build time): settings,
; saves, logs, texture packs and anything the player added stay, and so does the folder if anything is left in it.
Unicode true
SetCompressor /SOLID lzma
SetCompressorDictSize 64
RequestExecutionLevel user
ManifestDPIAware true

!include "MUI2.nsh"

Name "Athelas ${VER}"
OutFile "${OUT}"
InstallDir "C:\Games\Athelas"
InstallDirRegKey HKCU "Software\Athelas" "InstallDir"
BrandingText "Athelas ${VER}"
VIProductVersion "${VERNUM}"
VIAddVersionKey "ProductName" "Athelas"
VIAddVersionKey "FileDescription" "Athelas setup"
VIAddVersionKey "FileVersion" "${VER}"
VIAddVersionKey "LegalCopyright" "MIT licence, by Shagg with Claude (Anthropic)"

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_DIRECTORYPAGE_TEXT_TOP "Athelas lets you play your own copies of four Lord of the Rings games. No game files are included.$\r$\n$\r$\nClick Install to put it in the folder below (or Browse for another one: not Program Files, and not a game's folder)."
!define MUI_FINISHPAGE_TITLE "Athelas is installed"
!define MUI_FINISHPAGE_TEXT "Start it from the Start menu or the desktop. In the launcher, pick a game and show it your copy: your installed Return of the King (PC), or your own US Xbox disc image (.iso).$\r$\n$\r$\nNot affiliated with or endorsed by EA, New Line Cinema, Warner Bros. or the Tolkien Estate."
!define MUI_FINISHPAGE_RUN "$INSTDIR\Athelas.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start Athelas"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!macro REFUSE_UNDER ROOT
  ; $INSTDIR is ROOT itself or inside it (ROOT + "\"): refused (Next stays grey; the page's text says why)
  StrLen $1 "${ROOT}"
  StrCpy $0 $INSTDIR $1
  StrCmp $0 "${ROOT}" 0 +5
    StrCpy $2 $INSTDIR 1 $1
    StrCmp $2 "" +2
    StrCmp $2 "\" 0 +2
      Abort
!macroend
Function .onVerifyInstDir
  ; Program Files (both, on 64-bit Windows) needs admin rights Athelas doesn't ask for, and settings couldn't be saved
  !insertmacro REFUSE_UNDER "$PROGRAMFILES"
  !insertmacro REFUSE_UNDER "$PROGRAMFILES64"
FunctionEnd

Section "Athelas" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; the mods' .ini files hold the player's settings for them: an install over an existing folder keeps them
  File /r /x "*.ini" "${SRC}\*.*"
  SetOverwrite off
  File /r "${SRC}\*.ini"
  SetOverwrite on
  WriteUninstaller "$INSTDIR\Uninstall Athelas.exe"
  WriteRegStr HKCU "Software\Athelas" "InstallDir" "$INSTDIR"
  ; Settings > Apps lists it, for the player's own uninstall
  !define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Athelas"
  WriteRegStr HKCU "${UNKEY}" "DisplayName" "Athelas"
  WriteRegStr HKCU "${UNKEY}" "DisplayVersion" "${VER}"
  WriteRegStr HKCU "${UNKEY}" "Publisher" "Shagg"
  WriteRegStr HKCU "${UNKEY}" "DisplayIcon" "$INSTDIR\Athelas.exe"
  WriteRegStr HKCU "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall Athelas.exe"'
  WriteRegDWORD HKCU "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNKEY}" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\Athelas"
  CreateShortcut "$SMPROGRAMS\Athelas\Athelas.lnk" "$INSTDIR\Athelas.exe"
  CreateShortcut "$SMPROGRAMS\Athelas\Uninstall Athelas.lnk" "$INSTDIR\Uninstall Athelas.exe"
SectionEnd

Section "-Desktop shortcut"
  CreateShortcut "$DESKTOP\Athelas.lnk" "$INSTDIR\Athelas.exe"
SectionEnd

Section "Uninstall"
  MessageBox MB_YESNO|MB_ICONQUESTION "If you play The Return of the King (PC) with ROTK Fix, open Athelas first and use About > Remove ROTK Fix, so your game goes back to how it was.$\r$\n$\r$\nUninstall Athelas now? Your settings, saves and texture packs are kept." /SD IDYES IDYES +2
    Abort
  !include "${FILES_INC}"
  Delete "$INSTDIR\athelas-installed.txt"
  Delete "$INSTDIR\Uninstall Athelas.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\Athelas\Athelas.lnk"
  Delete "$SMPROGRAMS\Athelas\Uninstall Athelas.lnk"
  RMDir "$SMPROGRAMS\Athelas"
  Delete "$DESKTOP\Athelas.lnk"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Athelas"
  DeleteRegKey HKCU "Software\Athelas"
SectionEnd
; Tested 12 Nov 2026 under Wine 9 (1.8.3 archive): silent install (/S /D=C:\Games\Athelas) put all 176 files in place
; with matching hashes plus the Start menu shortcuts; the silent uninstall removed every shipped file and shortcut and
; kept Athelas.ini and TT-Xbox\saves\slot1 (made after the install). Files a later update adds aren't in the
; uninstaller's list: they and their folders stay (nothing of the player's is ever removed by mistake).
