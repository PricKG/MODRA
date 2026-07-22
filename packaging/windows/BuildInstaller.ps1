[CmdletBinding()]
param(
    [string]$ReleaseExecutable,
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($ReleaseExecutable)) {
    $ReleaseExecutable = Join-Path $projectRoot "build\release-windows\Release\modra.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $projectRoot "MODRA-Setup-0.1.0.exe"
}
$ReleaseExecutable = [IO.Path]::GetFullPath($ReleaseExecutable)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

if (-not (Test-Path -LiteralPath $ReleaseExecutable -PathType Leaf)) {
    throw "No existe el ejecutable Release: $ReleaseExecutable"
}
$version = & $ReleaseExecutable --version
if ($LASTEXITCODE -ne 0 -or $version -ne "MODRA 0.1.0") {
    throw "El ejecutable Release no corresponde a MODRA 0.1.0."
}

$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot "build"))
$installerRoot = [IO.Path]::GetFullPath((Join-Path $buildRoot "installer-windows"))
if (-not $installerRoot.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar,
                                   [StringComparison]::OrdinalIgnoreCase)) {
    throw "La carpeta temporal del instalador no es segura."
}
if (Test-Path -LiteralPath $installerRoot) {
    Remove-Item -LiteralPath $installerRoot -Recurse -Force
}
$stage = Join-Path $installerRoot "stage"
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $ReleaseExecutable -Destination (Join-Path $stage "modra.exe")
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "install.cmd") -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "install.ps1") -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "uninstall.ps1") -Destination $stage

$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "No se encontro vswhere.exe para localizar el runtime de Visual C++."
}
$visualStudio = & $vswhere -latest -products * -property installationPath
$redistRoot = Join-Path $visualStudio "VC\Redist\MSVC"
foreach ($dll in @("MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll")) {
    $runtime = Get-ChildItem -Path $redistRoot -Filter $dll -Recurse -File -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -match '[\\/]x64[\\/]Microsoft\.VC14[0-9]\.CRT[\\/]' } |
               Sort-Object FullName -Descending |
               Select-Object -First 1
    if (-not $runtime) {
        throw "No se encontro el runtime requerido: $dll"
    }
    Copy-Item -LiteralPath $runtime.FullName -Destination (Join-Path $stage $dll)
}

$iexpress = Join-Path $env:WINDIR "System32\iexpress.exe"
if (-not (Test-Path -LiteralPath $iexpress)) {
    throw "IExpress no esta disponible en este Windows."
}
if (Test-Path -LiteralPath $OutputPath) {
    Remove-Item -LiteralPath $OutputPath -Force
}

$sedPath = Join-Path $installerRoot "MODRA-Setup.sed"
$sourceDirectory = $stage.TrimEnd("\") + "\"
$sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=0
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=
FinishMessage=%FinishMessage%
TargetName=$OutputPath
FriendlyName=MODRA 0.1.0
AppLaunched=cmd.exe /d /c install.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=cmd.exe /d /c install.cmd
UserQuietInstCmd=cmd.exe /d /c install.cmd
SourceFiles=SourceFiles
[Strings]
InstallPrompt=Instalar MODRA 0.1.0?
FinishMessage=La instalacion de MODRA finalizo.
FILE0="modra.exe"
FILE1="MSVCP140.dll"
FILE2="VCRUNTIME140.dll"
FILE3="VCRUNTIME140_1.dll"
FILE4="install.cmd"
FILE5="install.ps1"
FILE6="uninstall.ps1"
[SourceFiles]
SourceFiles0=$sourceDirectory
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
%FILE3%=
%FILE4%=
%FILE5%=
%FILE6%=
"@
Set-Content -LiteralPath $sedPath -Value $sed -Encoding ASCII

$process = Start-Process -FilePath $iexpress -ArgumentList @("/N", "/Q", $sedPath) -Wait -PassThru
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
    throw "IExpress no pudo generar el instalador."
}
$hash = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath ($OutputPath + ".sha256") -Value "$hash  $([IO.Path]::GetFileName($OutputPath))" -Encoding ASCII
Write-Host "Instalador generado: $OutputPath"
Write-Host "SHA-256: $hash"
