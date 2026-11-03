; mini-daw installer (P9b, SPEC-PKG-02). NSIS 3, Unicode.
; Built by `cmake --build <build> --target installer`, which passes:
;   VERSION   e.g. 0.1.0
;   STAGE     folder with mini-daw.exe, mini-daw.com, LICENSE.txt, THIRD_PARTY.md, README.md, mini-daw.ico
;   OUTFILE   the installer to write
; Installs for the current user only (no administrator rights): into
; %LOCALAPPDATA%\Programs\mini-daw, with a Start menu entry and an uninstaller
; in Apps & features. Config, log and projects are the user's and are kept on
; uninstall. Part of mini-daw, MIT licence.

Unicode true
!include "MUI2.nsh"
!include "WordFunc.nsh"

; Separator before a file name in STAGE paths. The Windows makensis splits a
; `File` path at the last backslash to find the folder to search; with only
; '/' it found nothing (crosscheck #8, O0). The Linux makensis uses '/'.
!ifdef NSIS_WIN32_MAKENSIS
  !define SEP "\"
!else
  !define SEP "/"
!endif

!ifndef VERSION
  !error "VERSION is not defined"
!endif

Name "mini-daw ${VERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\mini-daw"
InstallDirRegKey HKCU "Software\mini-daw" "InstallDir"
SetCompressor /SOLID lzma
BrandingText "mini-daw ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "mini-daw"
VIAddVersionKey "FileDescription" "mini-daw installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) Maarten Boot. MIT licence."
VIAddVersionKey "CompanyName" "Maarten Boot"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\mini-daw"

; A running program cannot be replaced or removed (crosscheck #8, O4). Opening
; it for writing fails while it runs; then ask to close it. Silent: give up.
!macro WAIT_UNTIL_CLOSED DIR
  !define ID ${__LINE__}
  retry_${ID}:
  IfFileExists "${DIR}\mini-daw.exe" 0 done_${ID}
  ClearErrors
  FileOpen $9 "${DIR}\mini-daw.exe" a
  IfErrors busy_${ID}
  FileClose $9
  IfFileExists "${DIR}\mini-daw.com" 0 done_${ID}
  ClearErrors
  FileOpen $9 "${DIR}\mini-daw.com" a
  IfErrors busy_${ID}
  FileClose $9
  Goto done_${ID}
  busy_${ID}:
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "mini-daw is running. Close it (your project is saved automatically), then choose Retry." /SD IDCANCEL IDRETRY retry_${ID}
  Abort "mini-daw is running; nothing was changed."
  done_${ID}:
  !undef ID
!macroend

; An older installer over a newer installation: ask first (O5).
Function .onInit
  ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "DisplayVersion"
  StrCmp $0 "" done
  ${VersionCompare} "$0" "${VERSION}" $1
  StrCmp $1 "1" 0 done
  MessageBox MB_YESNO|MB_ICONQUESTION "A newer mini-daw ($0) is installed. Replace it with the older version ${VERSION}?" /SD IDNO IDYES done
  Abort
  done:
FunctionEnd

!define MUI_ICON "${STAGE}${SEP}mini-daw.ico"
!define MUI_UNICON "${STAGE}${SEP}mini-daw.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\mini-daw.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start mini-daw"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}${SEP}LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "mini-daw (required)" SecMain
  SectionIn RO
  ; An earlier installation in another folder is removed first, so no stray
  ; copy stays behind (O5).
  ReadRegStr $1 HKCU "${UNINSTALL_KEY}" "InstallLocation"
  StrCmp $1 "" same_folder
  StrCmp $1 "$INSTDIR" same_folder
  IfFileExists "$1\uninstall.exe" 0 same_folder
  !insertmacro WAIT_UNTIL_CLOSED "$1"
  ExecWait '"$1\uninstall.exe" /S _?=$1'
  Delete "$1\uninstall.exe"
  RMDir "$1"
  same_folder:
  !insertmacro WAIT_UNTIL_CLOSED "$INSTDIR"
  SetOutPath "$INSTDIR"
  File "${STAGE}${SEP}mini-daw.exe"
  File "${STAGE}${SEP}mini-daw.com"
  File "${STAGE}${SEP}LICENSE.txt"
  File "${STAGE}${SEP}THIRD_PARTY.md"
  File "${STAGE}${SEP}README.md"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateShortcut "$SMPROGRAMS\mini-daw.lnk" "$INSTDIR\mini-daw.exe"

  WriteRegStr HKCU "Software\mini-daw" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "mini-daw"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Maarten Boot"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\mini-daw.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" 14000
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\mini-daw.lnk" "$INSTDIR\mini-daw.exe"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "The program (mini-daw.exe), its command-line version (mini-daw.com) and a Start menu entry."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A mini-daw shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  !insertmacro WAIT_UNTIL_CLOSED "$INSTDIR"
  ; Only what the installer put there. Settings, the log (%APPDATA%\mini-daw)
  ; and projects and takes (Documents\mini-daw) belong to the user and stay.
  Delete "$INSTDIR\mini-daw.exe"
  Delete "$INSTDIR\mini-daw.com"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\THIRD_PARTY.md"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\mini-daw.lnk"
  Delete "$DESKTOP\mini-daw.lnk"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "Software\mini-daw"
SectionEnd
