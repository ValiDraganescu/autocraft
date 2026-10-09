# One-time setup of the Windows build machine (docs/builds.md): the build
# tools UE 5.8 asks for (Engine/Config/Windows/Windows_SDK.json), git, the
# AWS CLI and the repo. `build_release.sh setup` runs it as SYSTEM through
# SSM; it is rerunnable. It downloads, but does not run, the two installers
# that need a person: the Epic Games Launcher (a sign-in) and the Linux cross
# toolchain (Engine/Config/Linux/Linux_SDK.json).
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # Invoke-WebRequest crawls with the bar on
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Downloads = 'C:\build\downloads'
New-Item -ItemType Directory -Force -Path $Downloads, 'C:\build\out', 'C:\build\logs', 'C:\build\scripts' | Out-Null

function Get-Installer([string] $Url, [string] $Name) {
	$Path = Join-Path $Downloads $Name
	if (-not (Test-Path $Path)) {
		Write-Host "download $Url"
		Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing
	}
	$Path
}

function Install([string] $What, [string] $Exe, [string[]] $ArgList) {
	Write-Host "install $What"
	$P = Start-Process -FilePath $Exe -ArgumentList $ArgList -Wait -PassThru -NoNewWindow
	# 3010: done, wants a reboot (the AMI step reboots).
	if ($P.ExitCode -ne 0 -and $P.ExitCode -ne 3010) { throw "$What exited $($P.ExitCode)" }
}

# First the two that need a person, on the desktop: the engine download
# (tens of GB) can start while the rest installs.
$Launcher = Get-Installer 'https://launcher-public-service-prod06.ol.epicgames.com/launcher/api/installer/download/EpicGamesLauncherInstaller.msi' 'EpicGamesLauncherInstaller.msi'
$Toolchain = Get-Installer 'https://cdn.unrealengine.com/CrossToolchain_Linux/v26_clang-20.1.8-rockylinux8.exe' 'v26_clang-20.1.8-rockylinux8.exe'
$Desktop = 'C:\Users\Public\Desktop'
Copy-Item $Launcher, $Toolchain -Destination $Desktop -Force

# Visual Studio 2022 Build Tools: the C++ workload, the newest MSVC 14.44
# (17.14, the version the engine prefers) and the Windows SDK it names.
$Vs = 'C:\BuildTools'
if (-not (Test-Path "$Vs\VC\Tools\MSVC")) {
	$Exe = Get-Installer 'https://aka.ms/vs/17/release/vs_BuildTools.exe' 'vs_BuildTools.exe'
	Install 'Visual Studio Build Tools' $Exe @(
		'--quiet', '--wait', '--norestart', '--nocache', '--installPath', $Vs,
		'--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended',
		# The engine's suggested toolset. Its folder says 14.44.35207, but
		# cl.exe inside is newer than the banned 14.44.0-35210, and UBT reads
		# the version from cl.exe (MicrosoftPlatformSDK.cs).
		'--add', 'Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64',
		'--add', 'Microsoft.VisualStudio.Component.Windows11SDK.22621',
		'--add', 'Microsoft.Net.Component.4.6.2.TargetingPack',
		# UBT needs a .NET Framework SDK (NETFXSDK) for SwarmInterface.
		'--add', 'Microsoft.Net.Component.4.8.SDK')
}

# Git for Windows, the newest 64-bit release.
$Git = 'C:\Program Files\Git\cmd\git.exe'
if (-not (Test-Path $Git)) {
	$Release = Invoke-RestMethod 'https://api.github.com/repos/git-for-windows/git/releases/latest'
	$Asset = $Release.assets | Where-Object { $_.name -match '^Git-.*-64-bit\.exe$' } | Select-Object -First 1
	$Exe = Get-Installer $Asset.browser_download_url $Asset.name
	Install 'Git' $Exe @('/VERYSILENT', '/NORESTART', '/NOCANCEL', '/SP-')
}
# The build runs as SYSTEM through SSM; the clone is SYSTEM's too, but a
# person on the machine (Administrator) must be able to use it.
& $Git config --system --replace-all safe.directory '*'
& $Git config --system core.longpaths true

# AWS CLI v2: the build uploads its zips and log with it.
$Aws = 'C:\Program Files\Amazon\AWSCLIV2\aws.exe'
if (-not (Test-Path $Aws)) {
	$Msi = Get-Installer 'https://awscli.amazonaws.com/AWSCLIV2.msi' 'AWSCLIV2.msi'
	Install 'AWS CLI' 'msiexec.exe' @('/i', $Msi, '/qn', '/norestart')
}

# Defender scans every file a compile or a cook writes: leave the build out.
foreach ($Dir in 'C:\build', 'C:\Program Files\Epic Games', $Vs) {
	Add-MpPreference -ExclusionPath $Dir -ErrorAction SilentlyContinue
}

# The repo, public, by HTTPS. Each build fetches and checks out its commit.
$Repo = 'C:\build\autocraft'
if (-not (Test-Path "$Repo\.git")) {
	Write-Host 'clone the repo'
	& $Git clone --quiet https://github.com/ValiDraganescu/ringshadow.git $Repo
	if ($LASTEXITCODE -ne 0) { throw "git clone exited $LASTEXITCODE" }
}

Write-Host ''
Write-Host 'Setup done. Left for a person, over remote desktop (docs/builds.md):'
Write-Host "  1. $Desktop\EpicGamesLauncherInstaller.msi: sign in, install UE 5.8 with the Linux target platform"
Write-Host "  2. $Desktop\v26_clang-20.1.8-rockylinux8.exe: the Linux cross toolchain (sets LINUX_MULTIARCH_ROOT)"
