$ErrorActionPreference = 'Stop'
$SkillHome = if ($env:TIBO_SKILL_HOME) { $env:TIBO_SKILL_HOME } else { Join-Path $HOME '.agents/skills' }
$Destination = Join-Path $SkillHome 'tibo-reset'
if (Test-Path -LiteralPath $Destination) { throw "Already exists: $Destination. Move the old folder to a backup before reinstalling." }
New-Item -ItemType Directory -Force -Path $SkillHome | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tibo-reset') -Destination $Destination -Recurse
Write-Host "Installed lightweight skill: $Destination"
Write-Host 'The app is downloaded on first use.'
