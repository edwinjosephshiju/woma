!include "MUI2.nsh"
!include "LogicLib.nsh"

Name "WomaPython 3.12.13"
OutFile "woma-windows-x86_64-installer.exe"
InstallDir "$PROGRAMFILES64\Woma"
RequestExecutionLevel admin

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Section "Install Files" SecInstall
  SetOutPath "$INSTDIR"
  
  ; Copy all files from the release directory
  File /r "release-woma\*"
  
  ExecWait `powershell.exe -NoProfile -Command "$$p = [Environment]::GetEnvironmentVariable('Path', [EnvironmentVariableTarget]::Machine); if ($$p -notmatch [regex]::Escape('$INSTDIR')) { [Environment]::SetEnvironmentVariable('Path', $$p + ';$INSTDIR', [EnvironmentVariableTarget]::Machine) }"`

SectionEnd

Section "Download AI Model (1.28 GB)" SecModel
  SetOutPath "$INSTDIR"
  
  DetailPrint "Downloading Qwen2.5-Coder model..."
  ; We use ExecWait to pop up a visible PowerShell window so the user sees the progress
  ExecWait `powershell.exe -NoProfile -Command "& { Write-Host 'Downloading WomaPython AI Model (1.28 GB)... Do not close this window.'; Invoke-WebRequest -Uri 'https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q5_k_m.gguf' -OutFile '$INSTDIR\model.gguf'; Write-Host 'Download complete!'; Start-Sleep -Seconds 2 }"`
SectionEnd
