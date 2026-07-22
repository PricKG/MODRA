[CmdletBinding()]
param([switch]$SkipSystemIntegration)

$ErrorActionPreference = "Stop"
$installDirectory = [IO.Path]::GetFullPath($PSScriptRoot)

if (-not $SkipSystemIntegration) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $entries = @($userPath -split ";" | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and $_.TrimEnd("\") -ine $installDirectory.TrimEnd("\")
    })
    [Environment]::SetEnvironmentVariable("Path", ($entries -join ";"), "User")

    Remove-Item -LiteralPath (Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\MODRA.lnk") `
        -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MODRA" `
        -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "MODRA fue desinstalado. Los datos personales de MODRA no fueron eliminados."
Remove-Item -LiteralPath $installDirectory -Recurse -Force
