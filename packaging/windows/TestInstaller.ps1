[CmdletBinding()]
param(
    [string]$InstallerPath,
    [string]$TestRoot
)

$ErrorActionPreference = "Stop"
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($InstallerPath)) {
    $InstallerPath = Join-Path $projectRoot "dist\MODRA-Setup.exe"
}
if ([string]::IsNullOrWhiteSpace($TestRoot)) {
    $TestRoot = Join-Path $projectRoot "build\installer-smoke"
}
$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
$TestRoot = [IO.Path]::GetFullPath($TestRoot)
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot "build"))
if (-not $TestRoot.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar,
                              [StringComparison]::OrdinalIgnoreCase)) {
    throw "La carpeta de prueba debe estar dentro de: $buildRoot"
}
if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
    throw "No existe el instalador: $InstallerPath"
}

$checksumPath = "$InstallerPath.sha256"
if (-not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) {
    throw "No existe el checksum: $checksumPath"
}
$expectedHash = ((Get-Content -LiteralPath $checksumPath -Raw).Trim() -split '\s+')[0]
$actualHash = (Get-FileHash -LiteralPath $InstallerPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($expectedHash -ne $actualHash) {
    throw "El checksum SHA-256 del instalador no coincide."
}

if (Test-Path -LiteralPath $TestRoot) {
    Remove-Item -LiteralPath $TestRoot -Recurse -Force
}
$installDirectory = Join-Path $TestRoot "installed"
$localAppData = Join-Path $TestRoot "local-app-data"
New-Item -ItemType Directory -Path $localAppData -Force | Out-Null

$installProcess = Start-Process -FilePath $InstallerPath -ArgumentList @(
    "/VERYSILENT",
    "/SUPPRESSMSGBOXES",
    "/NORESTART",
    "/NOICONS",
    "/TASKS=",
    "/DIR=$installDirectory"
) -Wait -PassThru -WindowStyle Hidden
if ($installProcess.ExitCode -ne 0) {
    throw "La instalación silenciosa falló con código $($installProcess.ExitCode)."
}
$installedExecutable = Join-Path $installDirectory "modra.exe"
if (-not (Test-Path -LiteralPath $installedExecutable -PathType Leaf)) {
    throw "El instalador no copió modra.exe."
}

$version = (& $installedExecutable --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $version -notmatch '^MODRA \d+\.\d+\.\d+$') {
    throw "El ejecutable instalado no respondió correctamente: $version"
}
$previousLocalAppData = $env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA = $localAppData
    & $installedExecutable doctor
    if ($LASTEXITCODE -ne 0) {
        throw "El diagnóstico de la instalación falló."
    }
} finally {
    $env:LOCALAPPDATA = $previousLocalAppData
}

$uninstaller = Join-Path $installDirectory "unins000.exe"
if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
    throw "La instalación no generó el desinstalador."
}
$uninstallProcess = Start-Process -FilePath $uninstaller -ArgumentList @(
    "/VERYSILENT",
    "/SUPPRESSMSGBOXES",
    "/NORESTART"
) -Wait -PassThru -WindowStyle Hidden
if ($uninstallProcess.ExitCode -ne 0) {
    throw "La desinstalación silenciosa falló con código $($uninstallProcess.ExitCode)."
}
if (Test-Path -LiteralPath $installedExecutable) {
    throw "La desinstalación no eliminó modra.exe."
}

Write-Host "Instalador validado: $version"
Write-Host "Checksum, instalación, doctor y desinstalación: OK"
