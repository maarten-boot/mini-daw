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

!define MUI_ICON "${STAGE}/mini-daw.ico"
!define MUI_UNICON "${STAGE}/mini-daw.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\mini-daw.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start mini-daw"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}/LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "mini-daw (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${STAGE}/mini-daw.exe"
  File "${STAGE}/mini-daw.com"
  File "${STAGE}/LICENSE.txt"
  File "${STAGE}/THIRD_PARTY.md"
  File "${STAGE}/README.md"
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
