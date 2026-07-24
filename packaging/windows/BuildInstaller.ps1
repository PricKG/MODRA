[CmdletBinding()]
param(
    [string]$ReleaseExecutable,
    [string]$OutputDirectory,
    [string]$InnoCompiler
)

$ErrorActionPreference = "Stop"
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($ReleaseExecutable)) {
    $ReleaseExecutable = Join-Path $projectRoot "build\release-windows\Release\modra.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot "dist"
}
$ReleaseExecutable = [IO.Path]::GetFullPath($ReleaseExecutable)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

if (-not (Test-Path -LiteralPath $ReleaseExecutable -PathType Leaf)) {
    throw "No existe el ejecutable Release: $ReleaseExecutable"
}
$versionOutput = (& $ReleaseExecutable --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch '^MODRA (?<Version>\d+\.\d+\.\d+)$') {
    throw "El ejecutable no informó una versión válida de MODRA: $versionOutput"
}
$version = $Matches.Version

if ([string]::IsNullOrWhiteSpace($InnoCompiler)) {
    $command = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($command) {
        $InnoCompiler = $command.Source
    } else {
        $candidates = @(
            (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
            (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
        )
        $InnoCompiler = $candidates | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_ -PathType Leaf)
        } | Select-Object -First 1
    }
}
if ([string]::IsNullOrWhiteSpace($InnoCompiler) -or
    -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw "No se encontró ISCC.exe. Instalá Inno Setup 6 o indicá -InnoCompiler."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $dumpbin = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -find "VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe" | Select-Object -First 1
    if ($dumpbin) {
        $dependencies = & $dumpbin /dependents $ReleaseExecutable | Out-String
        if ($dependencies -match '(?i)\b(?:MSVCP|VCRUNTIME)14\d(?:_1)?\.dll\b') {
            throw "modra.exe depende del runtime de Visual C++. Configurá el runtime estático antes de empaquetar."
        }
    }
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$installerPath = Join-Path $OutputDirectory "MODRA-Setup.exe"
$checksumPath = "$installerPath.sha256"
Remove-Item -LiteralPath $installerPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $checksumPath -Force -ErrorAction SilentlyContinue

$scriptPath = Join-Path $PSScriptRoot "MODRA.iss"
& $InnoCompiler `
    "/DMyAppVersion=$version" `
    "/DSourceExecutable=$ReleaseExecutable" `
    "/DOutputDirectory=$OutputDirectory" `
    $scriptPath
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Inno Setup no pudo generar el instalador."
}

$hash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath $checksumPath -Value "$hash  MODRA-Setup.exe" -Encoding ASCII

Write-Host "Instalador generado: $installerPath"
Write-Host "Versión: $version"
Write-Host "SHA-256: $hash"
