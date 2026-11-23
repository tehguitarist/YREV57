; YREV57 VST3 installer (NSIS). Windows has no AU format, so there's no plugin-type choice
; here — VST3 only, installed to the shared system VST3 folder.
;
; Build with (from repo root, after building YREV57_VST3):
;   bash installer/stage_docs.sh build/docs 0.4.0
;   makensis /DVERSION=0.4.0 /DARTEFACTS_DIR=build\YREV57_artefacts\Release\VST3 /DDOCS_DIR=build\docs installer\windows\YREV57.nsi
;
; ARTEFACTS_DIR should point at the directory CONTAINING "YREV57.vst3" (i.e. the VST3 release
; output folder), not the bundle itself. DOCS_DIR is the readme and licences staged by stage_docs.sh,
; installed to Program Files\YREV57 (not into the signed bundle).

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef ARTEFACTS_DIR
  !define ARTEFACTS_DIR "..\..\build\YREV57_artefacts\Release\VST3"
!endif
!ifndef DOCS_DIR
  !define DOCS_DIR "..\..\build\docs"
!endif

Name "YREV57"
OutFile "YREV57-Windows-v${VERSION}-Installer.exe"
InstallDir "$COMMONFILES64\VST3"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "YREV57 VST3 Plugin" SecVST3
    SetOutPath "$INSTDIR\YREV57.vst3"
    File /r "${ARTEFACTS_DIR}\YREV57.vst3\*.*"

    WriteUninstaller "$INSTDIR\YREV57.vst3\Uninstall-YREV57.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YREV57" \
        "DisplayName" "YREV57 VST3"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YREV57" \
        "UninstallString" "$INSTDIR\YREV57.vst3\Uninstall-YREV57.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YREV57" \
        "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YREV57" \
        "Publisher" "YREV57 Project"
SectionEnd

Section "Read me and licences" SecDocs
    SectionIn RO
    SetOutPath "$PROGRAMFILES64\YREV57"
    File /r "${DOCS_DIR}\*.*"
SectionEnd

Section "Uninstall"
    RMDir /r "$INSTDIR\YREV57.vst3"
    RMDir /r "$PROGRAMFILES64\YREV57"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YREV57"
SectionEnd
