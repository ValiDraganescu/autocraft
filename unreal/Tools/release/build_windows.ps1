# One release build on the Windows build machine (docs/builds.md): check out
# a commit, package the game for each platform with UAT BuildCookRun, zip
# each one and upload the zips and the log to S3. `build_release.sh build`
# runs it as SYSTEM through SSM. The Linux package is cross-compiled with
# Epic's toolchain; the Mac repacks it to set the executable bits that a
# Windows archive cannot carry.
param(
	[Parameter(Mandatory = $true)] [string] $Ref,      # a commit on GitHub
	[Parameter(Mandatory = $true)] [string] $Version,  # the game's version; names the zips: Ringshadow-<Version>-windows.zip
	[Parameter(Mandatory = $true)] [string] $Bucket,
	[string] $BucketRegion = 'eu-north-1',             # the bucket's, wherever the machine runs
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

	# The Visual C++ runtime goes next to the game's exe (UAT's app-local
	# prerequisites), so a PC without it starts the game anyway: the DLLs of
	# the compiler that built it, from the Build Tools' redistributable folder.
	# UAT looks in <dir>\Win64\x64\<any folder>.
	$AppLocal = 'C:\build\applocal'
	$Vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
	$Crt = Get-Item "$Vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
		Sort-Object { [version]($_.Parent.Parent.Name -replace '[^0-9.]', '') } | Select-Object -Last 1
	if (-not $Crt) { throw "no Visual C++ redistributable folder in the Build Tools ($Vs\VC\Redist\MSVC)" }
	if (Test-Path $AppLocal) { Remove-Item -Recurse -Force $AppLocal }
	New-Item -ItemType Directory -Force "$AppLocal\Win64\x64\VC.CRT" | Out-Null
	Copy-Item "$($Crt.FullName)\*.dll" "$AppLocal\Win64\x64\VC.CRT"
	Say "app-local runtime: $($Crt.FullName) ($((Get-ChildItem "$AppLocal\Win64\x64\VC.CRT").Count) DLLs)"

	$Zips = @()
	foreach ($Platform in $Platforms.Split(',')) {
		$Archive = "$Out\$Platform"
		if (Test-Path $Archive) { Remove-Item -Recurse -Force $Archive }
		$UatArgs = @('BuildCookRun', "-project=$Project", '-target=Autocraft', "-platform=$Platform",
			"-clientconfig=$Config", '-build', '-cook', '-stage', '-pak', '-iostore', '-compressed',
			'-archive', "-archivedirectory=$Archive\Ringshadow", '-nodebuginfo', '-utf8output', '-unattended', '-nop4',
			# The game's version in the exe's Product version (Shipping) and the logs.
			"-ubtargs=-BuildVersion=$Version")
		if ($Platform -eq 'Win64') { $UatArgs += '-prereqs', "-applocaldirectory=$AppLocal" }
		Run "package $Platform" { & $Uat @UatArgs }

		# UAT archives the launcher (Autocraft.exe, Autocraft.sh), the project
		# folder and Engine side by side, straight into Ringshadow.
		if (-not (Test-Path "$Archive\Ringshadow\Autocraft\Binaries")) { throw "UAT archived no game for $Platform" }
		if ($Platform -eq 'Win64') {
			if (-not (Test-Path "$Archive\Ringshadow\Autocraft\Binaries\Win64\vcruntime140.dll")) { throw 'the Visual C++ runtime is not next to the game' }
			$Zip = "$Out\Ringshadow-$Version-windows.zip"
			Run "zip $Platform" { tar.exe -a -c -f $Zip -C $Archive Ringshadow }
			# The game's symbols stay out of the package (-nodebuginfo) but go
			# up beside it: next to the exe, they turn a crash report's
			# addresses into a call stack (`build_release.sh test` puts them there).
			$Bin = "$Repo\unreal\Binaries\Win64"
			$Pdbs = @(Get-ChildItem $Bin -Filter "Autocraft*-$Config.pdb" -ErrorAction SilentlyContinue)
			if ($Pdbs.Count -eq 0) { Say "no Autocraft*-$Config.pdb in $Bin; no symbols this time" }
			else {
				$Symbols = "$Out\Ringshadow-$Version-windows-symbols.zip"
				Run 'zip symbols' { tar.exe -a -c -f $Symbols -C $Bin @($Pdbs | ForEach-Object Name) }
				$Zips += $Symbols
			}
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
		Run "upload $(Split-Path -Leaf $File)" { & $Aws s3 cp --region $BucketRegion --only-show-errors $File "$Dest/" }
	}
	Say 'done'
} catch {
	Say "FAILED: $_"
	throw
} finally {
	$Writer.Close()
	& $Aws s3 cp --region $BucketRegion --only-show-errors $Log "$Dest/build.log"
}
exit 0
