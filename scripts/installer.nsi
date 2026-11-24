; 99Hertz Windows installer for one VST3 plugin.
; Defines passed by CI: PRODUCT ("AutoPress Pro"), VERSION ("1.0.0"), REGKEY ("AutoPressPro"), OUTFILE.
; The .vst3 bundle "${PRODUCT}.vst3" must sit next to this script.
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

Name "${PRODUCT} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\99Hertz\${PRODUCT}"
RequestExecutionLevel admin
BrandingText "99Hertz"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "CompanyName" "99Hertz"
VIAddVersionKey "FileDescription" "${PRODUCT} VST3 installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "99Hertz"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_TEXT "${PRODUCT} is installed to Common Files\VST3.$\r$\n$\r$\nRescan plugins in your DAW to find it."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${PRODUCT} needs 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "${PRODUCT} VST3" SecMain
  ; Replace any older copy completely
  RMDir /r "$COMMONFILES64\VST3\${PRODUCT}.vst3"
  SetOutPath "$COMMONFILES64\VST3"
  File /r "${PRODUCT}.vst3"

  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall ${PRODUCT}.exe"

  !define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\99Hertz ${REGKEY}"
  WriteRegStr HKLM "${UNKEY}" "DisplayName" "${PRODUCT} (VST3)"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNKEY}" "Publisher" "99Hertz"
  WriteRegStr HKLM "${UNKEY}" "URLInfoAbout" "https://99hertz.com"
  WriteRegStr HKLM "${UNKEY}" "InstallLocation" "$COMMONFILES64\VST3\${PRODUCT}.vst3"
  WriteRegStr HKLM "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall ${PRODUCT}.exe"'
  WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  RMDir /r "$COMMONFILES64\VST3\${PRODUCT}.vst3"
  Delete "$INSTDIR\Uninstall ${PRODUCT}.exe"
  RMDir "$INSTDIR"
  RMDir "$PROGRAMFILES64\99Hertz"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\99Hertz ${REGKEY}"
SectionEnd
