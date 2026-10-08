# One release build on the Windows build machine (docs/builds.md): check out
# a commit, package the game for each platform with UAT BuildCookRun, zip
# each one and upload the zips and the log to S3. `build_release.sh build`
# runs it as SYSTEM through SSM. The Linux package is cross-compiled with
# Epic's toolchain; the Mac repacks it to set the executable bits that a
# Windows archive cannot carry.
param(
	[Parameter(Mandatory = $true)] [string] $Ref,      # a commit on GitHub
	[Parameter(Mandatory = $true)] [string] $Version,  # names the zips: Ringshadow-<Version>-windows.zip
	[Parameter(Mandatory = $true)] [string] $Bucket,
	[string] $Platforms = 'Win64,Linux',
	[string] $Config = 'Shipping',
	[string] $Engine = 'C:\Program Files\Epic Games\UE_5.8'
)
$ErrorActionPreference = 'Stop'

# Full paths: the SSM agent's PATH is the one it started with, which can
# predate the installs on the setup machine.
$Git = 'C:\Program Files\Git\cmd\git.exe'
$Aws = 'C:\Program Files\Amazon\AWSCLIV2\aws.exe'
$Uat = Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat'
$Repo = 'C:\build\autocraft'
$Project = "$Repo\unreal\Autocraft.uproject"
$Out = "C:\build\out\$Version"
$Log = "C:\build\logs\$Version.log"
$Dest = "s3://$Bucket/builds/$Version"
# UBT runs `git status` for its adaptive unity build.
$env:Path = "$(Split-Path $Git);$env:Path"
if (-not $env:LINUX_MULTIARCH_ROOT) {
	$env:LINUX_MULTIARCH_ROOT = [Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'Machine')
}

# One writer for the whole log, UTF-8 (Windows PowerShell's Tee-Object and
# Set-Content would mix in UTF-16 and the ANSI code page).
New-Item -ItemType Directory -Force -Path (Split-Path $Log), $Out | Out-Null
$Writer = New-Object IO.StreamWriter($Log, $false, (New-Object Text.UTF8Encoding($false)))
$Writer.AutoFlush = $true

function Say([string] $Text) {
	$Line = "$(Get-Date -Format 'HH:mm:ss') $Text"
	Write-Host $Line
	$Writer.WriteLine($Line)
}

# A native command, its output in the log; fails on a non-zero exit. Git and
# UAT write progress to stderr, which must not count as an error here.
function Run([string] $What, [scriptblock] $Block) {
	Say $What
	$ErrorActionPreference = 'Continue'
	& $Block 2>&1 | ForEach-Object { $Writer.WriteLine("$_"); "$_" } | Out-Host
	$Code = $LASTEXITCODE
	$ErrorActionPreference = 'Stop'
	if ($Code -ne 0) { throw "$What failed (exit $Code)" }
}

Say "Ringshadow $Version, $Ref, $Platforms, $Config"
try {
	if (-not (Test-Path $Uat)) { throw "no engine at $Engine (install UE 5.8 from the Epic Games Launcher)" }
	if ($Platforms -match 'Linux' -and -not $env:LINUX_MULTIARCH_ROOT) {
		throw 'LINUX_MULTIARCH_ROOT is not set (install the Linux cross toolchain, then reboot)'
	}

	Run 'fetch' { & $Git -C $Repo fetch --force --tags --prune origin }
	Run "check out $Ref" { & $Git -C $Repo checkout --force --detach $Ref }
	# Untracked files go; ignored ones (Binaries, Intermediate, the DDC) stay,
	# so a machine made from a warmed image builds incrementally.
	Run 'clean' { & $Git -C $Repo clean -fd }
	$Commit = (& $Git -C $Repo rev-parse HEAD).Trim()
	Say "commit $Commit"

	$Zips = @()
	foreach ($Platform in $Platforms.Split(',')) {
		$Archive = "$Out\$Platform"
		if (Test-Path $Archive) { Remove-Item -Recurse -Force $Archive }
		$UatArgs = @('BuildCookRun', "-project=$Project", '-target=Autocraft', "-platform=$Platform",
			"-clientconfig=$Config", '-build', '-cook', '-stage', '-pak', '-iostore', '-compressed',
			'-archive', "-archivedirectory=$Archive", '-nodebuginfo', '-utf8output', '-unattended', '-nop4')
		if ($Platform -eq 'Win64') { $UatArgs += '-prereqs' }
		Run "package $Platform" { & $Uat @UatArgs }

		# UAT archives into one folder named for the platform (Windows, Linux).
		$Built = Get-ChildItem -Directory $Archive | Select-Object -First 1
		if (-not $Built) { throw "UAT archived nothing for $Platform" }
		Rename-Item $Built.FullName 'Ringshadow'
		if ($Platform -eq 'Win64') {
			$Zip = "$Out\Ringshadow-$Version-windows.zip"
			Run "zip $Platform" { tar.exe -a -c -f $Zip -C $Archive Ringshadow }
		} else {
			$Zip = "$Out\Ringshadow-$Version-$($Platform.ToLower()).tar.gz"
			Run "tar $Platform" { tar.exe -c -z -f $Zip -C $Archive Ringshadow }
		}
		$Zips += $Zip
	}

	$Manifest = "$Out\manifest.txt"
	Set-Content -Path $Manifest -Value @(
		"version $Version", "commit $Commit", "config $Config", "platforms $Platforms",
		"engine $((Get-Content (Join-Path $Engine 'Engine\Build\Build.version') | ConvertFrom-Json | ForEach-Object { "$($_.MajorVersion).$($_.MinorVersion).$($_.PatchVersion)" }))",
		"built $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))")
	foreach ($File in $Zips + $Manifest) {
		Run "upload $(Split-Path -Leaf $File)" { & $Aws s3 cp --only-show-errors $File "$Dest/" }
	}
	Say 'done'
} catch {
	Say "FAILED: $_"
	throw
} finally {
	$Writer.Close()
	& $Aws s3 cp --only-show-errors $Log "$Dest/build.log"
}
exit 0
