; Bandicoot installer for Windows (NSIS 3), built by scripts/package-windows.sh:
;
;     makensis -DVERSION=<version> -DSRCDIR=<packaged folder> -DOUTFILE=<installer>
;              [-DICON=<.ico>] bandicoot.nsi
;
; Modelled on WinCoot's installer: it unpacks into C:\Bandicoot (a folder
; without spaces, writable by its users), creates Start menu and desktop
; shortcuts that run bandicoot.bat, and asks an administrator whether to
; install for all users or only the current one.

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel highest

!ifndef VERSION
  !error "define VERSION"
!endif
!ifndef SRCDIR
  !error "define SRCDIR"
!endif
!ifndef OUTFILE
  !error "define OUTFILE"
!endif

!define APP "Bandicoot"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bandicoot"

Name "${APP} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "C:\Bandicoot"

!include "MUI2.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING
!ifdef ICON
  !define MUI_ICON "${ICON}"
  !define MUI_UNICON "${ICON}"
!endif
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\COPYING"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  SetShellVarContext current
  UserInfo::GetAccountType
  Pop $0
  ${If} $0 == "Admin"
    MessageBox MB_YESNO|MB_ICONQUESTION "Install ${APP} for all users of this computer?$\r$\n$\r$\n(No installs it for the current user only.)" IDNO done
    SetShellVarContext all
    done:
  ${EndIf}
FunctionEnd

Section "Bandicoot"
  ; An earlier version's program files are replaced, not merged with; user
  ; files in the folder (preferences, backups, state) are left alone.
  ${If} ${FileExists} "$INSTDIR\bandicoot.bat"
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\lib"
    RMDir /r "$INSTDIR\share"
    RMDir /r "$INSTDIR\etc"
  ${EndIf}

  SetOutPath "$INSTDIR"
  File /r "${SRCDIR}\*"

  ; the shortcuts start in the installation folder, as WinCoot's do
  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortcut "$SMPROGRAMS\${APP}\${APP}.lnk" "$INSTDIR\bandicoot.bat" "" "$INSTDIR\bin\Bandicoot.exe" 0
  CreateShortcut "$SMPROGRAMS\${APP}\Uninstall ${APP}.lnk" "$INSTDIR\uninstall.exe"
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\bandicoot.bat" "" "$INSTDIR\bin\Bandicoot.exe" 0

  WriteUninstaller "$INSTDIR\uninstall.exe"
  ; SHCTX is HKLM for an all-users install, HKCU otherwise
  WriteRegStr SHCTX "${UNINST_KEY}" "DisplayName" "${APP} ${VERSION}"
  WriteRegStr SHCTX "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr SHCTX "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\bin\Bandicoot.exe"
  WriteRegStr SHCTX "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr SHCTX "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD SHCTX "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD SHCTX "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Function un.onInit
  ; the context the installer used: all users if it registered under HKLM
  SetShellVarContext current
  ReadRegStr $0 HKLM "${UNINST_KEY}" "InstallLocation"
  ${If} $0 == "$INSTDIR"
    SetShellVarContext all
  ${EndIf}
FunctionEnd

Section "Uninstall"
  Delete "$DESKTOP\${APP}.lnk"
  RMDir /r "$SMPROGRAMS\${APP}"

  ; only what the installer put there: the user's preferences, backups and
  ; state files stay, and so does the folder if any are left
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\lib"
  RMDir /r "$INSTDIR\share"
  RMDir /r "$INSTDIR\etc"
  Delete "$INSTDIR\bandicoot.bat"
  Delete "$INSTDIR\COPYING"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\AUTHORS"
  Delete "$INSTDIR\THIRD_PARTY_LICENSES.md"
  Delete "$INSTDIR\CORRESPONDING_SOURCE.md"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey SHCTX "${UNINST_KEY}"
SectionEnd
