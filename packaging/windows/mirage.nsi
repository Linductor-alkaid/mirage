; M5-11 (DEC-031): NSIS installer for the Windows product processes.
; Ships: mirage.exe (CLI), mirage-service.exe, mirage-tray.exe and — when
; built — mirage-desktop.exe with its CEF payload (see MIRAGE_DESKTOP_DIR).
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
!ifndef MIRAGE_WITH_DESKTOP
!define MIRAGE_WITH_DESKTOP "0"
!endif

!define MIRAGE_BIN_DIR "..\..\build\release\bin"
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

    ; Desktop shell + CEF payload when the release tree carries them.
    !if "${MIRAGE_WITH_DESKTOP}" == "1"
        File "${MIRAGE_BIN_DIR}\mirage-desktop.exe"
        File /nonfatal "${MIRAGE_BIN_DIR}\chrome_elf.dll"
        File /nonfatal "${MIRAGE_BIN_DIR}\libcef.dll"
        File /nonfatal /r "${MIRAGE_BIN_DIR}\locales"
        File /nonfatal "${MIRAGE_BIN_DIR}\icudtl.dat"
        File /nonfatal "${MIRAGE_BIN_DIR}\resources.pak"
        ; CEF chrome-sandbox.exe ships setuid-equivalent via the installer
        ; ACL (the Windows sandbox model is bootstrap-mediated, M5-01).
        File /nonfatal "${MIRAGE_BIN_DIR}\vk_swiftshader.dll"
    !endif

    ; Start Menu shortcut (快速进入 Mirage).
    CreateDirectory "$SMPROGRAMS\Mirage"
    CreateShortcut "$SMPROGRAMS\Mirage\Mirage.lnk" "$INSTDIR\mirage-desktop.exe" "" \
        "$INSTDIR\mirage-desktop.exe" 0 SW_SHOWNORMAL "" ""

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
    Delete "$INSTDIR\mirage-desktop.exe"
    Delete "$INSTDIR\uninstall.exe"
    ; CEF payload files are removed with RMDir /r by the uninstaller policy:
    ; kept conservative here (only files the installer wrote).
    RMDir "$INSTDIR"
SectionEnd
