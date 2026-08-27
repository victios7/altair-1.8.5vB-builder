Unicode True
!include "MUI2.nsh"

!define APPNAME    "Altair"
!define APPVERSION "1.8.5"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Altair"

Name "Altair Language ${APPVERSION}"
OutFile "Altair-Setup-${APPVERSION}.exe"
InstallDir "$PROGRAMFILES64\Altair"
RequestExecutionLevel admin
ShowInstDetails show

SetCompressor lzma
SetCompressorDictSize 64

!define MUI_ABORTWARNING
!define MUI_ICON "ALTAIR_LOGO.ico"
!define MUI_UNICON "ALTAIR_LOGO.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Spanish"

Section "Altair Compiler"
    SetOutPath "$INSTDIR"
    FileOpen $1 "$TEMP\altair_exclude.ps1" w
    FileWrite $1 'Add-MpPreference -ExclusionPath "$INSTDIR"'
    FileClose $1
    ExecWait 'powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP\altair_exclude.ps1"' $0

    File "altairc.exe"
    File "altair-terminal.exe"
    File "ALTAIR_LOGO.ico"
    
    SetOutPath "$INSTDIR\runtime"
    File /r "runtime\*.*"
    
    SetOutPath "$INSTDIR\examples"
    File /r "examples\*.*"
    
    SetOutPath "$INSTDIR"
    File "mingw64.zip"
    DetailPrint "Instalando compilador C, esto puede tardar varios minutos..."
    ExecWait 'powershell -NoProfile -Command "Expand-Archive -Path \"$INSTDIR\mingw64.zip\" -DestinationPath \"$INSTDIR\" -Force"' $0
    Delete /REBOOTOK "$INSTDIR\mingw64.zip"
    
    ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
    
    Push "$INSTDIR;$INSTDIR\mingw64\bin"
    Push $0
    Call PathContains
    Pop $1
    
    ${If} $1 == 0
        WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
            "Path" "$0;$INSTDIR;$INSTDIR\mingw64\bin"
        SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment"
    ${EndIf}
    
    CreateShortcut "$DESKTOP\Altair Terminal.lnk" \
        "$INSTDIR\altair-terminal.exe" "" \
        "$INSTDIR\ALTAIR_LOGO.ico" 0
    
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName"     "Altair ${APPVERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion"  "${APPVERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher"       "Altair Language"
    WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\ALTAIR_LOGO.ico"
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Function PathContains
    Exch $0
    Exch
    Exch $1
    Push $2
    Push $3
    
    StrCpy $2 0
    StrLen $3 $1
    
    ${Do}
        StrCpy $2 $0 $3 $2
        ${If} $2 == ""
            ${Break}
        ${EndIf}
        
        ${If} $2 == $1
            StrCpy $3 1
            ${Break}
        ${EndIf}
        
        IntOp $2 $2 + 1
    ${Loop}
    
    Pop $3
    Pop $2
    Pop $1
    Exch $0
FunctionEnd

Section "Uninstall"
    Delete "$DESKTOP\Altair Terminal.lnk"
    Delete "$INSTDIR\altairc.exe"
    Delete "$INSTDIR\altair-terminal.exe"
    Delete "$INSTDIR\ALTAIR_LOGO.ico"
    Delete "$INSTDIR\uninstall.exe"
    
    RMDir /r "$INSTDIR\runtime"
    RMDir /r "$INSTDIR\mingw64"
    RMDir /r "$INSTDIR\examples"
    RMDir "$INSTDIR"
    
    DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
