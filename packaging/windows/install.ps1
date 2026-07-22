[CmdletBinding()]
param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA "Programs\MODRA"),
    [switch]$SkipSystemIntegration
)

$ErrorActionPreference = "Stop"
$InstallDirectory = [IO.Path]::GetFullPath($InstallDirectory)
$payload = @("modra.exe", "MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll", "uninstall.ps1")
foreach ($file in $payload) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $file) -PathType Leaf)) {
        throw "Falta un archivo requerido del instalador: $file"
    }
}

New-Item -ItemType Directory -Path $InstallDirectory -Force | Out-Null
foreach ($file in $payload) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $InstallDirectory $file) -Force
}

if (-not $SkipSystemIntegration) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $entries = @($userPath -split ";" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if (-not ($entries | Where-Object { $_.TrimEnd("\") -ieq $InstallDirectory.TrimEnd("\") })) {
        [Environment]::SetEnvironmentVariable("Path", ((@($entries) + $InstallDirectory) -join ";"), "User")
    }

    $startMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
    New-Item -ItemType Directory -Path $startMenu -Force | Out-Null
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $startMenu "MODRA.lnk"))
    $shortcut.TargetPath = (Join-Path $InstallDirectory "modra.exe")
    $shortcut.WorkingDirectory = $InstallDirectory
    $shortcut.Save()

    $uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MODRA"
    New-Item -Path $uninstallKey -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayName -Value "MODRA" -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value "0.1.0" -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name Publisher -Value "MODRA" -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $InstallDirectory -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayIcon -Value (Join-Path $InstallDirectory "modra.exe") -PropertyType String -Force | Out-Null
    $uninstallCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' +
                        (Join-Path $InstallDirectory "uninstall.ps1") + '"'
    New-ItemProperty -Path $uninstallKey -Name UninstallString -Value $uninstallCommand -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
}

& (Join-Path $InstallDirectory "modra.exe") --version
Write-Host "MODRA se instalo en: $InstallDirectory"
if ($SkipSystemIntegration) {
    Write-Host "PATH, menu Inicio y registro se omitieron durante la validacion."
} else {
    Write-Host "Abri una terminal nueva y ejecuta: modra"
}
