# One-time setup of the Windows tester, a GPU machine to play the packages on
# (docs/builds.md, "Testing the packages"): NVIDIA's driver for EC2 G
# instances (the GRID driver AWS licenses for its GPUs, with DirectX and
# Vulkan), Amazon DCV (a remote desktop that streams what the GPU draws) and
# the AWS CLI. `build_release.sh test` runs it as SYSTEM through SSM on a new
# g6.2xlarge, reboots the machine and saves it as the tester image. Rerunnable.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # Invoke-WebRequest crawls with the bar on
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Downloads = 'C:\tester\downloads'
New-Item -ItemType Directory -Force -Path $Downloads | Out-Null

function Install([string] $What, [string] $Exe, [string[]] $ArgList) {
	Write-Host "install $What"
	$P = Start-Process -FilePath $Exe -ArgumentList $ArgList -Wait -PassThru -NoNewWindow
	# 3010: done, wants a reboot (the script's caller reboots).
	if ($P.ExitCode -ne 0 -and $P.ExitCode -ne 3010) { throw "$What exited $($P.ExitCode)" }
}

# AWS CLI v2: the test command fetches the packages with it.
$Aws = 'C:\Program Files\Amazon\AWSCLIV2\aws.exe'
if (-not (Test-Path $Aws)) {
	$Msi = Join-Path $Downloads 'AWSCLIV2.msi'
	Invoke-WebRequest -Uri 'https://awscli.amazonaws.com/AWSCLIV2.msi' -OutFile $Msi -UseBasicParsing
	Install 'AWS CLI' 'msiexec.exe' @('/i', $Msi, '/qn', '/norestart')
}

# The GRID driver, from AWS's bucket (us-east-1; the instance role may read it).
if (-not (Get-CimInstance Win32_VideoController | Where-Object { $_.Name -match 'NVIDIA' })) {
	try {
		$Key = (Get-S3Object -BucketName ec2-windows-nvidia-drivers -KeyPrefix latest/ -Region us-east-1 |
			Where-Object { $_.Key -match '\.exe$' } | Sort-Object LastModified | Select-Object -Last 1).Key
	} catch {
		throw "cannot read s3://ec2-windows-nvidia-drivers (us-east-1): the organization's policy must let this account read it (docs/builds.md, Testing the packages). $_"
	}
	if (-not $Key) { throw 'no driver in s3://ec2-windows-nvidia-drivers/latest/' }
	Write-Host "driver $Key"
	$Driver = Join-Path $Downloads 'nvidia-grid.exe'
	Read-S3Object -BucketName ec2-windows-nvidia-drivers -Key $Key -File $Driver -Region us-east-1 | Out-Null
	Install 'NVIDIA GRID driver' $Driver @('-s', '-noreboot')
	# AWS's GRID build is licensed by the instance; hide the license page.
	New-Item -Path 'HKLM:\SOFTWARE\NVIDIA Corporation\Global\GridLicensing' -Force | Out-Null
	Set-ItemProperty -Path 'HKLM:\SOFTWARE\NVIDIA Corporation\Global\GridLicensing' -Name NvCplDisableManageLicensePage -Value 1 -Type DWord
}

# Amazon DCV: a console session for Administrator, on 8443 (reached through an
# SSM tunnel; the security group stays closed).
if (-not (Get-Service dcvserver -ErrorAction SilentlyContinue)) {
	$Msi = Join-Path $Downloads 'dcv-server.msi'
	Invoke-WebRequest -Uri 'https://d1uj6qtbmh3dt5.cloudfront.net/nice-dcv-server-x64-Release.msi' -OutFile $Msi -UseBasicParsing
	Install 'Amazon DCV' 'msiexec.exe' @('/i', $Msi, 'ADDLOCAL=ALL', 'AUTOMATIC_SESSION_OWNER=Administrator', '/quiet', '/norestart',
		'/l*v', (Join-Path $Downloads 'dcv-server.log'))
}

# Games stay out of Defender's scans.
Add-MpPreference -ExclusionPath 'C:\Ringshadow' -ErrorAction SilentlyContinue

Write-Host 'Tester setup done; it needs a reboot for the driver.'
