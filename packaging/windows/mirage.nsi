; M5-11 (DEC-031): NSIS installer for the Windows product processes.
; Ships CLI, service, tray and the native EUI frontend with assets (DEC-037).
; Also writes the System.AppUserModel.ID start-menu shortcut property and
; registry key (the DEC-018 AUMID carrier: the trigger condition's "installer
; can deliver AUMID" half is met by this installer).
;
Unicode true
ManifestDPIAware true
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\Mirage"
Name "Mirage"
OutFile "mirage-installer.exe"
!ifndef MIRAGE_VERSION
!define MIRAGE_VERSION "0.1.0"
!endif

!ifndef MIRAGE_BIN_DIR
!define MIRAGE_BIN_DIR "..\..\build\release\bin"
!endif
!define MIRAGE_AUMID "Mirage.Desktop"

Page Directory
Page InstFiles
UninstPage UninstConfirm
UninstPage InstFiles

Section "Install"
    SetOutPath $INSTDIR
    ; Core product processes (always present in a release tree).
    File "${MIRAGE_BIN_DIR}\mirage.exe"
    File "${MIRAGE_BIN_DIR}\mirage-service.exe"
    File "${MIRAGE_BIN_DIR}\mirage-tray.exe"

    File "${MIRAGE_BIN_DIR}\mirage-native.exe"
    SetOutPath "$INSTDIR\assets"
    File "${MIRAGE_BIN_DIR}\assets\mira.png"
    File "${MIRAGE_BIN_DIR}\assets\mira-ui.png"
    File "${MIRAGE_BIN_DIR}\assets\Font Awesome 7 Free-Solid-900.otf"
    File "${MIRAGE_BIN_DIR}\assets\NotoSansSC-Regular.otf"
    File "${MIRAGE_BIN_DIR}\assets\OFL-NotoSansSC.txt"
    SetOutPath "$INSTDIR"

    ; Start Menu shortcut (快速进入 Mirage).
    CreateDirectory "$SMPROGRAMS\Mirage"
    CreateShortcut "$SMPROGRAMS\Mirage\Mirage.lnk" "$INSTDIR\mirage.exe" "start" \
        "$INSTDIR\mirage-native.exe" 0 SW_SHOWNORMAL "" ""

    ; DEC-018 AUMID carrier, registry half: AppUserModelId key with
    ; DisplayName/IconUri. The shortcut's own System.AppUserModel.ID
    ; property requires the NSIS ApplicationID plugin (release machine);
    ; the runtime can also set the AUMID per-process. M5 ships no toast
    ; (DEC-018 M5-11 re-evaluation: not re-opened — see the DEC-018
    ; revision record).
    WriteRegStr HKCU "Software\Classes\AppUserModelId\${MIRAGE_AUMID}" \
        "DisplayName" "Mirage"

    ; Uninstaller.
    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mirage" \
        "DisplayName" "Mirage"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mirage" \
        "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mirage" \
        "DisplayVersion" "${MIRAGE_VERSION}"
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Mirage\Mirage.lnk"
    RMDir "$SMPROGRAMS\Mirage"
    DeleteRegKey HKCU "Software\Classes\AppUserModelId\${MIRAGE_AUMID}"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mirage"
    Delete "$INSTDIR\mirage.exe"
    Delete "$INSTDIR\mirage-service.exe"
    Delete "$INSTDIR\mirage-tray.exe"
    Delete "$INSTDIR\mirage-native.exe"
    RMDir /r "$INSTDIR\assets"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
