# Run from an x64 Native Tools Command Prompt / Developer PowerShell for Visual Studio.
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force "$PSScriptRoot/dist" | Out-Null
Push-Location "$PSScriptRoot/dist"
try {
    cl /nologo /std:c++17 /utf-8 /O1 /MT /EHsc /Gy /GL /DNDEBUG "$PSScriptRoot/src/main.cpp" /Fe:TiboReset.exe /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /LTCG advapi32.lib d2d1.lib dwrite.lib ole32.lib uuid.lib comctl32.lib shell32.lib gdi32.lib user32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    Copy-Item "$PSScriptRoot/../LICENSE" "$PSScriptRoot/dist/LICENSE" -Force
    Write-Host "EXE size: $((Get-Item TiboReset.exe).Length) bytes"
} finally { Pop-Location }
