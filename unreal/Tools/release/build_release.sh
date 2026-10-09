#!/bin/bash
# Release builds of Ringshadow (docs/builds.md). Windows and Linux are built on
# a Windows EC2 machine made on demand from a saved image (AMI); the Mac build
# is made here. Everything in AWS carries the tag Project=ringshadow.
#
#   build_release.sh infra                  bucket, instance role, security group, key pair (once; rerunnable)
#   build_release.sh setup                  start the setup machine from Windows Server and install the build tools
#   build_release.sh rdp [INSTANCE]         remote desktop (RDP) to the setup machine or the tester: localhost:13389
#   build_release.sh image [INSTANCE]       save the setup machine as the build image, then terminate it
#   build_release.sh build [REF] [options]  build REF (default HEAD) on a spot machine made from the newest image
#       --version V        the game's version and the zips' name (default: git describe of REF, without the v)
#       --platforms LIST   Win64,Linux (default) or one of them
#       --config C         Shipping (default) or Development
#       --instance ID      build on a running machine (the setup one, to warm it) instead of a new one
#       --on-demand        an on-demand machine instead of spot
#       --keep             leave the machine running afterwards (stop it with `stop`)
#   build_release.sh mac [--version V] [--config C]   package the Mac build on this Mac
#   build_release.sh sign APP ZIP           sign, notarize and zip a Mac .app (mac does it)
#   build_release.sh test [VERSION] [options]  a GPU machine (g6.2xlarge) with VERSION's Windows package
#                                           (default: the newest), then the remote desktop to it
#       --linux            Ubuntu and the Linux package instead; checks the game reaches the home screen
#       --on-demand        an on-demand machine instead of spot
#       --hours H          it switches itself off and is terminated after H hours (default 4)
#   build_release.sh dcv [INSTANCE]         remote desktop to the tester through SSM: https://localhost:18443
#   build_release.sh status                 machines, images and builds in AWS
#   build_release.sh stop                   terminate every running Ringshadow machine (builds and testers)
#
#   Before the command, --region R runs the machines in region R (default
#   eu-north-1; us-east-1 when Stockholm has no room). `--region R infra`
#   prepares R once: its security group, the key pair, copies of the images.
#
# The builds land in unreal/Saved/Releases/<version>/ (not in git).
# AUTOCRAFT_AWS_PROFILE picks the AWS profile (default: ringshadow).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
releases="$repo/unreal/Saved/Releases"

export AWS_PROFILE=${AUTOCRAFT_AWS_PROFILE:-ringshadow}
# The machines run in Stockholm unless --region (or AUTOCRAFT_AWS_REGION)
# says otherwise; the bucket, the images' originals and the SSM output stay
# in Stockholm.
home_region=eu-north-1
export AWS_REGION=${AUTOCRAFT_AWS_REGION:-$home_region}
export AWS_PAGER=""

role=ringshadow-build
group=ringshadow-build
key=ringshadow-build
keyfile="$HOME/.ssh/ringshadow-build.pem"
# The notarytool credentials (xcrun notarytool store-credentials ringshadow).
notary_profile=${AUTOCRAFT_NOTARY_PROFILE:-ringshadow}
# The Mac app's identity (the project's own is the template's com.YourCompany.Autocraft).
bundle_id=com.validraganescu.ringshadow
# 32 vCPU, the whole standard-instance quota, for setup too: its first
# build compiles every shader into the cache the image keeps.
build_type=c7i.8xlarge
disk_gb=400
# The tester: 8 vCPU of the G quota, DirectX 12 and Vulkan. An NVIDIA L4
# first, then an A10G, then a T4, whichever has room (one GRID driver serves
# all three).
tester_types="g6.2xlarge g5.2xlarge g4dn.2xlarge"
tester_disk_gb=100

say() { printf '%s %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { printf 'build_release: %s\n' "$*" >&2; exit 1; }

account() { aws sts get-caller-identity --query Account --output text; }
bucket() { echo "ringshadow-builds-$(account)"; }
# The bucket's commands, which go to Stockholm from any region.
s3() { aws --region "$home_region" s3 "$@"; }

group_id() {
	aws ec2 describe-security-groups --filters "Name=group-name,Values=$group" \
		--query 'SecurityGroups[0].GroupId' --output text
}

# A JSON string for one line of PowerShell.
json() { printf '"%s"' "$(printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')"; }

# ---------------------------------------------------------------- infra

cmd_infra() {
	local b; b=$(bucket)
	if ! aws --region "$home_region" s3api head-bucket --bucket "$b" >/dev/null 2>&1; then
		say "bucket $b"
		aws --region "$home_region" s3api create-bucket --bucket "$b" --create-bucket-configuration "LocationConstraint=$home_region" >/dev/null
		aws --region "$home_region" s3api put-public-access-block --bucket "$b" --public-access-block-configuration \
			BlockPublicAcls=true,IgnorePublicAcls=true,BlockPublicPolicy=true,RestrictPublicBuckets=true
		# Builds are kept on the Mac and on GitHub; the bucket only carries them.
		aws --region "$home_region" s3api put-bucket-lifecycle-configuration --bucket "$b" --lifecycle-configuration \
			'{"Rules":[{"ID":"expire","Status":"Enabled","Filter":{"Prefix":""},"Expiration":{"Days":60}}]}'
	fi

	if ! aws iam get-role --role-name "$role" >/dev/null 2>&1; then
		say "role $role"
		aws iam create-role --role-name "$role" --description 'Ringshadow build machines: SSM and the build bucket' \
			--assume-role-policy-document '{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":{"Service":"ec2.amazonaws.com"},"Action":"sts:AssumeRole"}]}' >/dev/null
		aws iam attach-role-policy --role-name "$role" --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore
	fi
	aws iam put-role-policy --role-name "$role" --policy-name build-bucket --policy-document \
		"{\"Version\":\"2012-10-17\",\"Statement\":[{\"Effect\":\"Allow\",\"Action\":[\"s3:GetObject\",\"s3:PutObject\",\"s3:ListBucket\"],\"Resource\":[\"arn:aws:s3:::$b\",\"arn:aws:s3:::$b/*\"]}]}"
	# The tester: NVIDIA's driver for EC2 (us-east-1) and DCV's license check.
	aws iam put-role-policy --role-name "$role" --policy-name tester --policy-document \
		"{\"Version\":\"2012-10-17\",\"Statement\":[{\"Effect\":\"Allow\",\"Action\":[\"s3:GetObject\",\"s3:ListBucket\"],\"Resource\":[\"arn:aws:s3:::ec2-windows-nvidia-drivers\",\"arn:aws:s3:::ec2-windows-nvidia-drivers/*\",\"arn:aws:s3:::dcv-license.*/*\"]}]}"
	if ! aws iam get-instance-profile --instance-profile-name "$role" >/dev/null 2>&1; then
		say "instance profile $role"
		aws iam create-instance-profile --instance-profile-name "$role" >/dev/null
		aws iam add-role-to-instance-profile --instance-profile-name "$role" --role-name "$role"
		sleep 10  # a new profile takes a moment before EC2 accepts it
	fi

	# No inbound rules: SSM and remote desktop both go out from the machine.
	if [ "$(group_id)" = None ]; then
		say "security group $group"
		local vpc; vpc=$(aws ec2 describe-vpcs --filters Name=is-default,Values=true --query 'Vpcs[0].VpcId' --output text)
		aws ec2 create-security-group --group-name "$group" --vpc-id "$vpc" \
			--description 'Ringshadow build machines: outbound only' >/dev/null
	fi

	# The key only decrypts the Windows Administrator password. Every region
	# gets the same one: the first makes it, the others import its public half.
	if ! aws ec2 describe-key-pairs --key-names "$key" >/dev/null 2>&1; then
		if [ -e "$keyfile" ]; then
			say "key pair $key <- $keyfile"
			local pub; pub=$(mktemp)
			ssh-keygen -y -f "$keyfile" >"$pub"
			aws ec2 import-key-pair --key-name "$key" --public-key-material "fileb://$pub" >/dev/null
			rm "$pub"
		else
			say "key pair $key -> $keyfile"
			mkdir -p "$(dirname "$keyfile")"
			( umask 077; aws ec2 create-key-pair --key-name "$key" --query KeyMaterial --output text >"$keyfile" )
		fi
	fi

	# Another region gets copies of Stockholm's newest images (a copy takes
	# about an hour; until then build and test there say there is no image).
	if [ "$AWS_REGION" != "$home_region" ]; then
		local kind src name
		for kind in build tester; do
			src=$(AWS_REGION=$home_region newest_image "$kind")
			[ "$src" = None ] && continue
			name=$(aws --region "$home_region" ec2 describe-images --image-ids "$src" --query 'Images[0].Name' --output text)
			if [ "$(aws ec2 describe-images --owners self --filters "Name=name,Values=$name" --query 'length(Images)' --output text)" = 0 ]; then
				say "copying $name ($src) to $AWS_REGION"
				aws ec2 copy-image --source-region "$home_region" --source-image-id "$src" --name "$name" \
					--tag-specifications "ResourceType=image,Tags=[{Key=Project,Value=ringshadow},{Key=Name,Value=$name}]" \
					"ResourceType=snapshot,Tags=[{Key=Project,Value=ringshadow},{Key=Name,Value=$name}]" \
					--query ImageId --output text
			fi
		done
	fi
	say "infra ready in $AWS_REGION: bucket $b, role $role, group $group, key $keyfile"
}

# ---------------------------------------------------------------- machines

launch() {  # launch AMI TYPE ROLE NAME MARKET [DISK_GB]: prints the instance id
	# Tries each zone that offers TYPE (left to itself, EC2 picks one zone and
	# gives up when it is full). Returns 2 when no zone has room.
	local spot=()
	[ "$5" = spot ] && spot=(--instance-market-options 'MarketType=spot,SpotOptions={SpotInstanceType=one-time,InstanceInterruptionBehavior=terminate}')
	local t="{Key=Project,Value=ringshadow},{Key=Role,Value=$3},{Key=Name,Value=$4}"
	local zone subnet out
	for zone in $(aws ec2 describe-instance-type-offerings --location-type availability-zone \
		--filters "Name=instance-type,Values=$2" --query 'InstanceTypeOfferings[].Location' --output text); do
		subnet=$(aws ec2 describe-subnets --filters "Name=availability-zone,Values=$zone" Name=default-for-az,Values=true \
			--query 'Subnets[0].SubnetId' --output text)
		[ "$subnet" = None ] && continue
		# Switched off from inside, it is terminated (the tester's time limit).
		if out=$(aws ec2 run-instances --image-id "$1" --instance-type "$2" --count 1 \
			--key-name "$key" --security-group-ids "$(group_id)" --subnet-id "$subnet" \
			--iam-instance-profile "Name=$role" \
			--instance-initiated-shutdown-behavior terminate \
			--block-device-mappings "DeviceName=/dev/sda1,Ebs={VolumeSize=${6:-$disk_gb},VolumeType=gp3,Iops=6000,Throughput=500,DeleteOnTermination=true}" \
			--metadata-options HttpTokens=required \
			--tag-specifications "ResourceType=instance,Tags=[$t]" "ResourceType=volume,Tags=[$t]" \
			${spot[@]+"${spot[@]}"} \
			--query 'Instances[0].InstanceId' --output text 2>&1); then
			echo "$out"; return 0
		fi
		case $out in
		*InsufficientInstanceCapacity*) say "no $5 $2 in $zone" >&2 ;;
		*) printf '%s\n' "$out" >&2; return 1 ;;
		esac
	done
	return 2
}

state() { aws ec2 describe-instances --instance-ids "$1" --query 'Reservations[0].Instances[0].State.Name' --output text 2>/dev/null || echo gone; }

wait_ssm() {  # wait until the machine's SSM agent answers (Windows boots in about 5 min)
	local id=$1 t=0
	say "waiting for $id to boot and reach SSM"
	aws ec2 wait instance-running --instance-ids "$id"
	while [ "$(aws ssm describe-instance-information --filters "Key=InstanceIds,Values=$id" \
		--query 'InstanceInformationList[0].PingStatus' --output text)" != Online ]; do
		[ $t -ge 1200 ] && die "$id never reached SSM (20 min)"
		sleep 15; t=$((t + 15))
	done
	say "$id is up"
}

# run_ps INSTANCE TIMEOUT_S LABEL LINE...: runs PowerShell lines through SSM,
# waits, prints the end of the output on a failure. Output goes to the bucket.
# run_sh is the same for shell lines on Linux.
run_ps() { run_ssm AWS-RunPowerShellScript awsrunPowerShellScript "$@"; }
run_sh() { run_ssm AWS-RunShellScript awsrunShellScript "$@"; }
run_ssm() {
	local doc=$1 step=$2 id=$3 timeout=$4 label=$5; shift 5
	local b lines="" l
	b=$(bucket)
	for l in "$@"; do lines="$lines${lines:+,}$(json "$l")"; done
	local cmd
	cmd=$(aws ssm send-command --instance-ids "$id" --document-name "$doc" \
		--comment "ringshadow $label" --timeout-seconds 600 \
		--output-s3-bucket-name "$b" --output-s3-region "$home_region" --output-s3-key-prefix ssm \
		--parameters "{\"commands\":[$lines],\"executionTimeout\":[\"$timeout\"]}" \
		--query Command.CommandId --output text)
	say "$label: SSM command $cmd"
	# Where its output lands; ssm_output prints it.
	ssm_out="s3://$b/ssm/$cmd/$id/$step/0.$step"
	local status t=0
	while :; do
		sleep 30; t=$((t + 30))
		status=$(aws ssm get-command-invocation --command-id "$cmd" --instance-id "$id" \
			--query Status --output text 2>/dev/null || echo Pending)
		case $status in
			Pending|InProgress|Delayed) ;;
			Success) say "$label: done in $((t / 60)) min"; return 0 ;;
			*) break ;;
		esac
		[ "$(state "$id")" = running ] || { status="machine $(state "$id") (spot reclaimed?)"; break; }
		[ $((t % 300)) -eq 0 ] && say "$label: running, $((t / 60)) min"
	done
	say "$label: $status"
	local f
	for f in stdout stderr; do
		echo "--- $f (last 40 lines; whole: $ssm_out/$f)"
		s3 cp --only-show-errors "$ssm_out/$f" - 2>/dev/null | tail -40 || true
	done
	return 1
}
ssm_output() { s3 cp --only-show-errors "$ssm_out/stdout" - 2>/dev/null || true; }

running() {  # running ROLE: the newest such machine, or None
	local id
	id=$(aws ec2 describe-instances --filters Name=tag:Project,Values=ringshadow "Name=tag:Role,Values=$1" \
		Name=instance-state-name,Values=pending,running,stopping,stopped \
		--query 'sort_by(Reservations[].Instances[],&LaunchTime)[-1].InstanceId' --output text)
	echo "${id:-None}"
}
running_setup() { running setup; }

windows_base() {
	aws ssm get-parameter --name /aws/service/ami-windows-latest/Windows_Server-2022-English-Full-Base \
		--query Parameter.Value --output text
}

# The Administrator password (Windows makes it in the first minutes). A
# machine made from our own image (the tester) keeps the image's password
# and AWS never publishes one, so after a few minutes it gets a fresh one
# over SSM, kept in ~/.ssh for the later rdp and dcv calls.
password() {
	[ -r "$keyfile" ] || die "no $keyfile"
	local saved="$HOME/.ssh/ringshadow-$1.password"
	[ -s "$saved" ] && { cat "$saved"; return; }
	local pw="" tries=0
	if is_linux "$1"; then
		# Ubuntu's user has none: one is set for DCV's sign-in.
		pw="$(LC_ALL=C tr -dc 'A-Za-z0-9' </dev/urandom | head -c 20)Rs7"
		run_sh "$1" 60 password "echo 'ubuntu:$pw' | chpasswd" >&2 || die "could not set the password on $1"
		(umask 077; printf '%s' "$pw" > "$saved")
		printf '%s' "$pw"
		return
	fi
	while [ -z "$pw" ] && [ $tries -lt 9 ]; do
		pw=$(aws ec2 get-password-data --instance-id "$1" --priv-launch-key "$keyfile" --query PasswordData --output text)
		[ -z "$pw" ] && { say "the password is not ready yet" >&2; sleep 20; tries=$((tries + 1)); }
	done
	if [ -z "$pw" ]; then
		say "AWS has no password for $1; setting one" >&2
		# Letters and digits only: nothing for PowerShell or JSON to quote.
		pw="$(LC_ALL=C tr -dc 'A-Za-z0-9' </dev/urandom | head -c 20)Rs7"
		run_ps "$1" 120 password "net user Administrator '$pw' | Out-Null" "exit \$LASTEXITCODE" >&2 \
			|| die "could not set the password on $1"
	fi
	(umask 077; printf '%s' "$pw" > "$saved")
	printf '%s' "$pw"
}

is_linux() {
	[ "$(aws ec2 describe-instances --instance-ids "$1" --query 'Reservations[0].Instances[0].PlatformDetails' --output text)" = Linux/UNIX ]
}

cmd_setup() {
	cmd_infra
	local existing; existing=$(running_setup)
	[ "$existing" != None ] && die "a setup machine is already there: $existing (rdp, image or stop it)"
	local ami; ami=$(windows_base)
	say "launching the setup machine ($build_type, Windows Server 2022, $ami)"
	local id; id=$(launch "$ami" "$build_type" setup ringshadow-build-setup on-demand)
	say "setup machine $id"
	wait_ssm "$id"
	s3 cp --only-show-errors "$here/setup_windows.ps1" "s3://$(bucket)/scripts/setup_windows.ps1"
	# The base image has the AWS Tools for PowerShell but not the CLI yet.
	run_ps "$id" 7200 setup \
		"\$ErrorActionPreference = 'Stop'" \
		"New-Item -ItemType Directory -Force C:\\build\\scripts | Out-Null" \
		"Read-S3Object -BucketName '$(bucket)' -Key scripts/setup_windows.ps1 -File C:\\build\\scripts\\setup_windows.ps1 -Region $home_region | Out-Null" \
		"& C:\\build\\scripts\\setup_windows.ps1" \
		"exit 0"
	say "next: build_release.sh rdp, then the two installers on its desktop (docs/builds.md)"
}

cmd_rdp() {
	local id=${1:-$(running_setup)}
	[ "$id" = None ] && id=$(running tester)
	[ "$id" = None ] && die "no setup machine or tester; give an instance id"
	local pw; pw=$(password "$id")
	echo
	echo "  Connect a remote desktop app (Windows App, from the Mac App Store) to:"
	echo "    PC:       localhost:13389"
	echo "    User:     Administrator"
	echo "    Password: $pw"
	echo
	echo "  The tunnel stays open while this runs; Ctrl-C closes it."
	echo
	aws ssm start-session --target "$id" --document-name AWS-StartPortForwardingSession \
		--parameters 'portNumber=["3389"],localPortNumber=["13389"]'
}

cmd_image() {
	local id=${1:-$(running_setup)}
	[ "$id" = None ] && die "no setup machine; give an instance id"
	local name; name="ringshadow-build-$(date +%Y%m%d-%H%M)"
	say "saving $id as $name (Windows reboots for a clean disk)"
	local ami
	ami=$(aws ec2 create-image --instance-id "$id" --name "$name" \
		--description 'Ringshadow build machine: UE 5.8, VS Build Tools, the Linux cross toolchain' \
		--tag-specifications \
			"ResourceType=image,Tags=[{Key=Project,Value=ringshadow},{Key=Name,Value=$name}]" \
			"ResourceType=snapshot,Tags=[{Key=Project,Value=ringshadow},{Key=Name,Value=$name}]" \
		--query ImageId --output text)
	local s t=0
	# A new image can take a moment to be found at all.
	while s=$(aws ec2 describe-images --image-ids "$ami" --query 'Images[0].State' --output text 2>/dev/null || echo pending)
		[ "$s" = pending ]; do
		sleep 30; t=$((t + 30))
		[ $((t % 300)) -eq 0 ] && say "image $ami: still saving, $((t / 60)) min"
	done
	[ "$s" = available ] || die "image $ami: $s"
	say "image $ami ready"
	aws ec2 terminate-instances --instance-ids "$id" >/dev/null
	say "terminated $id. Builds now start from $ami."
}

newest_image() {  # newest_image [build|tester]
	aws ec2 describe-images --owners self --filters Name=tag:Project,Values=ringshadow Name=state,Values=available \
		"Name=name,Values=ringshadow-${1:-build}-*" \
		--query 'sort_by(Images,&CreationDate)[-1].ImageId' --output text
}

# ---------------------------------------------------------------- build

cmd_build() {
	local ref=HEAD version="" platforms=Win64,Linux config=Shipping instance="" market=spot keep=0
	while [ $# -gt 0 ]; do
		case $1 in
			--version) version=$2; shift 2 ;;
			--platforms) platforms=$2; shift 2 ;;
			--config) config=$2; shift 2 ;;
			--instance) instance=$2; shift 2 ;;
			--on-demand) market=on-demand; shift ;;
			--keep) keep=1; shift ;;
			-*) die "unknown option $1" ;;
			*) ref=$1; shift ;;
		esac
	done

	# The machine clones from GitHub, so the commit must be pushed.
	local sha; sha=$(git -C "$repo" rev-parse --verify "$ref^{commit}") || die "no commit $ref"
	git -C "$repo" fetch --quiet origin
	if [ -z "$(git -C "$repo" branch -r --contains "$sha")" ] && \
		! git -C "$repo" ls-remote --tags origin | grep -q "^$sha"; then
		die "$sha is not on GitHub yet; push it first"
	fi
	[ -n "$version" ] || version=$(git -C "$repo" describe --tags --always "$sha")
	version=${version#v}
	local b; b=$(bucket)
	say "building Ringshadow $version ($sha, $platforms, $config)"

	local id=$instance launched=0
	if [ -z "$id" ]; then
		local ami; ami=$(newest_image)
		[ "$ami" = None ] && die "no build image yet: run setup, then image (docs/builds.md)"
		say "launching a $market $build_type from $ami"
		id=$(launch "$ami" "$build_type" build "ringshadow-build-$version" "$market")
		launched=1
		say "build machine $id"
		if [ $keep = 0 ]; then
			# shellcheck disable=SC2064
			trap "say 'terminating $id'; aws ec2 terminate-instances --instance-ids $id >/dev/null" EXIT
		fi
	fi
	wait_ssm "$id"

	s3 cp --only-show-errors "$here/build_windows.ps1" "s3://$b/scripts/build_windows.ps1"
	run_ps "$id" 14400 build \
		"\$ErrorActionPreference = 'Stop'" \
		"& 'C:\\Program Files\\Amazon\\AWSCLIV2\\aws.exe' s3 cp --region $home_region --only-show-errors s3://$b/scripts/build_windows.ps1 C:\\build\\scripts\\build_windows.ps1" \
		"& C:\\build\\scripts\\build_windows.ps1 -Ref $sha -Version '$version' -Bucket '$b' -BucketRegion $home_region -Platforms '$platforms' -Config $config" \
		"exit 0" \
		|| { say "build log: s3://$b/builds/$version/build.log"; exit 1; }

	local dir="$releases/$version"
	mkdir -p "$dir"
	s3 cp --only-show-errors --recursive "s3://$b/builds/$version/" "$dir/"
	fix_linux "$dir/Ringshadow-$version-linux.tar.gz"
	( cd "$dir" && shasum -a 256 Ringshadow-* >SHA256SUMS )
	[ $launched = 1 ] && [ $keep = 1 ] && say "left $id running (--keep): build_release.sh stop"
	say "done: $dir"
	ls -lh "$dir"
}

# A tar made on Windows has no executable bits; set them on the launcher
# script and the Linux binaries, and pack it again as root-owned.
fix_linux() {
	local tgz=$1
	[ -f "$tgz" ] || return 0
	local tmp; tmp=$(mktemp -d)
	tar -xzf "$tgz" -C "$tmp"
	find "$tmp/Ringshadow" -name '*.sh' -exec chmod 755 {} +
	find "$tmp/Ringshadow" -type f -path '*/Binaries/Linux/*' -exec chmod 755 {} +
	COPYFILE_DISABLE=1 tar --uid 0 --gid 0 --uname root --gname root --no-mac-metadata -czf "$tgz" -C "$tmp" Ringshadow
	rm -r "$tmp"
	say "Linux: executable bits set"
}

cmd_mac() {
	local version="" config=Shipping
	while [ $# -gt 0 ]; do
		case $1 in
			--version) version=$2; shift 2 ;;
			--config) config=$2; shift 2 ;;
			*) die "unknown option $1" ;;
		esac
	done
	[ -n "$version" ] || version=$(git -C "$repo" describe --tags --always --dirty)
	version=${version#v}
	local dir="$releases/$version" uat="/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/RunUAT.sh"
	local archive="$dir/mac"
	rm -rf "$archive"
	mkdir -p "$dir"
	say "packaging the Mac build $version ($config) from the working tree"
	"$uat" BuildCookRun -project="$repo/unreal/Autocraft.uproject" -target=Autocraft -platform=Mac \
		-clientconfig="$config" -build -cook -stage -pak -iostore -compressed \
		-nodebuginfo -utf8output -unattended -nop4 -ubtargs="-BuildVersion=$version" \
		>"$dir/mac-build.log" 2>&1 || die "UAT failed: $dir/mac-build.log"
	# The staged app, which holds the game: UAT's -archive copies the bare
	# one from Binaries, without Contents/UE (2026-10-09).
	local staged; staged=$(find "$repo/unreal/Saved/StagedBuilds/Mac" -maxdepth 1 -name '*.app' -type d | head -1)
	[ -n "$staged" ] && [ -d "$staged/Contents/UE/Autocraft/Content/Paks" ] || die "no staged app with paks in Saved/StagedBuilds/Mac"
	local app="$archive/Ringshadow.app" plist="$archive/Ringshadow.app/Contents/Info.plist"
	mkdir -p "$archive"
	ditto "$staged" "$app"
	/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier $bundle_id" -c "Set :CFBundleName Ringshadow" "$plist"
	/usr/libexec/PlistBuddy -c "Set :CFBundleDisplayName Ringshadow" "$plist" 2>/dev/null \
		|| /usr/libexec/PlistBuddy -c "Add :CFBundleDisplayName string Ringshadow" "$plist"
	# The game's version, not the engine's 5.8.3: 0.1.1 from the tag v0.1.1;
	# 0.1.1 and build 0.1.1.4 four commits after it. Untagged, the engine's stays.
	if [[ $version =~ ^([0-9]+\.[0-9]+\.[0-9]+)(-([0-9]+)-g)? ]]; then
		local short=${BASH_REMATCH[1]} n=${BASH_REMATCH[3]}
		/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $short" \
			-c "Set :CFBundleVersion $short${n:+.$n}" "$plist"
		say "version $short (build $short${n:+.$n})"
	fi
	cmd_sign "$app" "$dir/Ringshadow-$version-mac.zip"
}

# sign APP ZIP: signs the .app with the Developer ID, notarizes it, staples
# the ticket and zips it. Without the certificate it zips it unsigned; without
# the notary profile it zips it signed but not notarized.
cmd_sign() {
	local app=$1 zip=$2
	[ -d "$app" ] && [ -n "$zip" ] || die "usage: build_release.sh sign APP ZIP"
	local identity=${AUTOCRAFT_SIGN_IDENTITY:-$(security find-identity -v -p codesigning |
		sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' | head -1)}
	rm -f "$zip"
	# Xcode signs the staged app ad hoc with the App Sandbox entitlement; the
	# renamed Info.plist breaks that signature, and macOS kills a sandboxed app
	# with a broken one at launch. Every path re-signs with no entitlements:
	# ad hoc ("-") without the certificate.
	local flags=(--force --timestamp --options runtime)
	if [ -z "$identity" ]; then identity=-; flags=(--force); fi
	say "signing with $identity"
	# Inside out: every Mach-O file first, then the bundle (--deep is
	# deprecated and skips files outside the code folders).
	local f
	while IFS= read -r f; do
		[[ $(file -b "$f") == *Mach-O* ]] || continue
		codesign "${flags[@]}" --sign "$identity" "$f" || die "codesign $f"
	done < <(find "$app/Contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \))
	codesign "${flags[@]}" --sign "$identity" "$app" || die "codesign $app"
	codesign --verify --deep --strict "$app" || die "the signature does not verify"
	ditto -c -k --keepParent "$app" "$zip"
	if [ "$identity" = - ]; then
		say "done: $zip, unsigned: ad hoc (no Developer ID Application certificate in the keychain)"
		return
	fi
	if ! xcrun notarytool history --keychain-profile "$notary_profile" >/dev/null 2>&1; then
		say "done: $zip, signed, not notarized (no notarytool profile \"$notary_profile\": docs/builds.md, Signing the Mac build)"
		return
	fi
	say "notarizing (a few minutes)"
	xcrun notarytool submit "$zip" --keychain-profile "$notary_profile" --wait --timeout 1h \
		>"$zip.notary.log" 2>&1 || true
	grep -q 'status: Accepted' "$zip.notary.log" \
		|| die "notarization failed: $zip.notary.log (xcrun notarytool log <id> --keychain-profile $notary_profile)"
	xcrun stapler staple "$app" || die "stapling failed"
	# The zip again, now with the ticket, so Gatekeeper passes offline too.
	rm -f "$zip"
	ditto -c -k --keepParent "$app" "$zip"
	spctl --assess --type execute "$app" || die "Gatekeeper rejects $app"
	say "done: $zip, signed and notarized"
}

# ---------------------------------------------------------------- test

# A GPU machine to play a Windows package on, over Amazon DCV. The first one
# starts from Windows Server and installs the NVIDIA driver and DCV
# (setup_tester.ps1), then is saved as the tester image; later ones start
# from that image.
cmd_test() {
	local version="" market=spot hours=4 linux=0
	while [ $# -gt 0 ]; do
		case $1 in
			--linux) linux=1; shift ;;
			--on-demand) market=on-demand; shift ;;
			--hours) hours=$2; shift 2 ;;
			-*) die "unknown option $1" ;;
			*) version=$1; shift ;;
		esac
	done
	cmd_infra >/dev/null
	[ $linux = 1 ] && { test_linux "$version" "$market" "$hours"; return; }
	local b; b=$(bucket)
	[ -n "$version" ] || version=$(s3 ls "s3://$b/builds/" --recursive | grep -- '-windows\.zip$' | sort | tail -1 | awk '{print $4}' | cut -d/ -f2)
	[ -n "$version" ] || die "no Windows package in s3://$b/builds/: build one first"
	local zip="Ringshadow-$version-windows.zip"
	s3 ls "s3://$b/builds/$version/$zip" >/dev/null || die "no s3://$b/builds/$version/$zip"

	local ami fresh=0; ami=$(newest_image tester)
	if [ "$ami" = None ]; then
		fresh=1
		ami=$(windows_base)
		say "no tester image yet: setting one up from Windows Server 2022 ($ami)"
	fi
	local id type rc
	for type in $tester_types; do
		say "launching a $market $type from $ami"
		rc=0; id=$(launch "$ami" "$type" tester "ringshadow-tester-$version" "$market" "$tester_disk_gb") || rc=$?
		[ $rc = 0 ] && break
		[ $rc = 2 ] || die "launch failed"
	done
	[ $rc = 0 ] || die "no $market GPU machine has room in $AWS_REGION; try again later"
	say "tester $id"
	wait_ssm "$id"

	if [ $fresh = 1 ]; then
		s3 cp --only-show-errors "$here/setup_tester.ps1" "s3://$b/scripts/setup_tester.ps1"
		run_ps "$id" 3600 tester-setup \
			"\$ErrorActionPreference = 'Stop'" \
			"New-Item -ItemType Directory -Force C:\\tester | Out-Null" \
			"Read-S3Object -BucketName '$b' -Key scripts/setup_tester.ps1 -File C:\\tester\\setup_tester.ps1 -Region $home_region | Out-Null" \
			"& C:\\tester\\setup_tester.ps1" \
			"exit 0" || die "tester setup failed; $id is still up (build_release.sh stop)"
		# Saving the image reboots the machine, which the driver needs anyway.
		local name; name="ringshadow-tester-$(date +%Y%m%d-%H%M)"
		say "saving $id as $name (it reboots)"
		aws ec2 create-image --instance-id "$id" --name "$name" \
			--description 'Ringshadow tester: Windows Server 2022, the NVIDIA GRID driver, Amazon DCV' \
			--tag-specifications \
				"ResourceType=image,Tags=[{Key=Project,Value=ringshadow},{Key=Name,Value=$name}]" \
				"ResourceType=snapshot,Tags=[{Key=Project,Value=ringshadow},{Key=Name,Value=$name}]" \
			--query ImageId --output text
		sleep 120
		wait_ssm "$id"
	fi

	run_ps "$id" 1800 stage \
		"\$ErrorActionPreference = 'Stop'" \
		"\$Gpu = (Get-CimInstance Win32_VideoController | Where-Object { \$_.Name -match 'NVIDIA' }).Name" \
		"if (-not \$Gpu) { throw 'no NVIDIA driver' }; Write-Host \"GPU: \$Gpu\"" \
		"\$Rdp = 'HKLM:\\SOFTWARE\\Policies\\Microsoft\\Windows NT\\Terminal Services'; New-Item -Path \$Rdp -Force | Out-Null" \
		"foreach (\$N in 'bEnumerateHWBeforeSW','AVC444ModePreferred','AVCHardwareEncodePreferred') { Set-ItemProperty -Path \$Rdp -Name \$N -Value 1 -Type DWord }" \
		"& 'C:\\Program Files\\Amazon\\AWSCLIV2\\aws.exe' s3 cp --region $home_region --only-show-errors s3://$b/builds/$version/$zip C:\\tester\\game.zip" \
		"if (Test-Path C:\\Ringshadow) { Remove-Item -Recurse -Force C:\\Ringshadow }" \
		"tar.exe -xf C:\\tester\\game.zip -C C:\\" \
		"\$Sym = 's3://$b/builds/$version/Ringshadow-$version-windows-symbols.zip'" \
		"if (Test-Path C:\\tester\\symbols.zip) { Remove-Item C:\\tester\\symbols.zip }" \
		"try { & 'C:\\Program Files\\Amazon\\AWSCLIV2\\aws.exe' s3 cp --region $home_region --only-show-errors \$Sym C:\\tester\\symbols.zip 2>\$null } catch { }" \
		"if (Test-Path C:\\tester\\symbols.zip) { tar.exe -xf C:\\tester\\symbols.zip -C C:\\Ringshadow\\Autocraft\\Binaries\\Win64; Write-Host 'symbols: next to the exe' } else { Write-Host 'symbols: none for this build' }" \
		"\$Exe = 'C:\\Ringshadow\\Autocraft\\Binaries\\Win64\\Autocraft-Win64-Shipping.exe'" \
		"Write-Host \"version: \$((Get-Item \$Exe).VersionInfo.ProductVersion)\"" \
		"# Like a player's PC: no runtime installed; the game brings its own. A short" \
		"# run without a GPU shows where vcruntime140.dll comes from." \
		"\$P = Start-Process \$Exe -ArgumentList '-nullrhi','-nosound','-unattended' -PassThru; Start-Sleep 20" \
		"\$Dll = (Get-Process -Id \$P.Id -ErrorAction SilentlyContinue).Modules | Where-Object ModuleName -eq 'vcruntime140.dll' | Select-Object -First 1" \
		"Stop-Process -Id \$P.Id -Force -ErrorAction SilentlyContinue" \
		"Write-Host \"runtime installed: \$(Test-Path 'HKLM:\\SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64')\"" \
		"Write-Host \"vcruntime140.dll: \$(if (\$Dll) { \$Dll.FileName } else { 'not loaded (the game did not stay up 20 s)' })\"" \
		"\$S = (New-Object -ComObject WScript.Shell).CreateShortcut('C:\\Users\\Public\\Desktop\\Ringshadow $version.lnk')" \
		"\$S.TargetPath = 'C:\\Ringshadow\\Autocraft.exe'; \$S.Save()" \
		"shutdown.exe /s /t $((hours * 3600)) /c 'Ringshadow tester: its $hours hours are up'" \
		"exit 0" || die "staging failed; $id is still up (build_release.sh stop)"
	ssm_output | grep -E '^(GPU|version|runtime installed|vcruntime140.dll|symbols):' || true
	say "ready: $id has $version on its desktop; it is terminated in $hours h (build_release.sh stop ends it sooner)"
	cmd_dcv "$id"
}

# The Linux tester: AWS's Ubuntu GPU image (NVIDIA driver included), the
# desktop and DCV set up on every start (setup_linux_tester.sh, 5 minutes;
# no saved image), VERSION's Linux package on the desktop. A first run of the
# game, silent, must still be up and on the GPU after a minute; its picture
# then lands in unreal/Saved/Releases/<version>/linux-home.png.
test_linux() {
	local version=$1 market=$2 hours=$3 b; b=$(bucket)
	[ -n "$version" ] || version=$(s3 ls "s3://$b/builds/" --recursive | grep -- '-linux\.tar\.gz$' | sort | tail -1 | awk '{print $4}' | cut -d/ -f2)
	[ -n "$version" ] || die "no Linux package in s3://$b/builds/: build one first"
	local tgz="Ringshadow-$version-linux.tar.gz"
	s3 ls "s3://$b/builds/$version/$tgz" >/dev/null || die "no s3://$b/builds/$version/$tgz"
	# Windows makes the tar without executable bits; the Mac's fixed copy goes up.
	local local_tgz="$releases/$version/$tgz"
	[ -f "$local_tgz" ] && s3 cp --only-show-errors "$local_tgz" "s3://$b/builds/$version/$tgz"

	local ami; ami=$(aws ssm get-parameter --name /aws/service/deeplearning/ami/x86_64/base-oss-nvidia-driver-gpu-ubuntu-24.04/latest/ami-id \
		--query Parameter.Value --output text)
	local id type rc
	for type in $tester_types; do
		say "launching a $market $type from Ubuntu 24.04 with the NVIDIA driver ($ami)"
		rc=0; id=$(launch "$ami" "$type" tester "ringshadow-linux-tester-$version" "$market" "$tester_disk_gb") || rc=$?
		[ $rc = 0 ] && break
		[ $rc = 2 ] || die "launch failed"
	done
	[ $rc = 0 ] || die "no $market GPU machine has room in $AWS_REGION; try --on-demand or --region us-east-1"
	say "Linux tester $id"
	wait_ssm "$id"

	s3 cp --only-show-errors "$here/setup_linux_tester.sh" "s3://$b/scripts/setup_linux_tester.sh"
	run_sh "$id" 1800 linux-setup \
		"set -e" \
		"aws s3 cp --region $home_region --only-show-errors s3://$b/scripts/setup_linux_tester.sh /root/setup_linux_tester.sh" \
		"bash /root/setup_linux_tester.sh" || die "setup failed; $id is still up (build_release.sh stop)"
	ssm_output | grep -E '^(GPU|Vulkan|DCV):' || true

	local shot="s3://$b/builds/$version/linux-home.png"
	run_sh "$id" 900 stage \
		"set -e" \
		"aws s3 cp --region $home_region --only-show-errors s3://$b/builds/$version/$tgz /tmp/game.tar.gz" \
		"rm -rf /home/ubuntu/Ringshadow" \
		"tar -xzf /tmp/game.tar.gz -C /home/ubuntu" \
		"chown -R ubuntu:ubuntu /home/ubuntu/Ringshadow" \
		"mkdir -p /home/ubuntu/Desktop" \
		"printf '[Desktop Entry]\\nType=Application\\nName=Ringshadow $version\\nExec=/home/ubuntu/Ringshadow/Autocraft.sh\\nPath=/home/ubuntu/Ringshadow\\nTerminal=false\\n' >/home/ubuntu/Desktop/ringshadow.desktop" \
		"chmod 755 /home/ubuntu/Desktop/ringshadow.desktop" \
		"chown -R ubuntu:ubuntu /home/ubuntu/Desktop" \
		"export DISPLAY=:0 XAUTHORITY=/home/ubuntu/.Xauthority" \
		"sudo -u ubuntu -E sh -c 'cd /home/ubuntu/Ringshadow && exec ./Autocraft.sh -nosound >/tmp/ringshadow-first-run.txt 2>&1' &" \
		"sleep 60" \
		"if pgrep -f Autocraft-Linux-Shipping >/dev/null; then echo 'game: up after 60 s'; else echo 'game: exited'; tail -30 /tmp/ringshadow-first-run.txt; fi" \
		"echo \"on the GPU: \$(nvidia-smi | grep -c Autocraft || true) process\"" \
		"sudo -u ubuntu -E import -window root /tmp/linux-home.png" \
		"aws s3 cp --region $home_region --only-show-errors /tmp/linux-home.png $shot" \
		"pkill -f Autocraft-Linux-Shipping || true" \
		"shutdown -h +$((hours * 60)) 'Ringshadow tester: its $hours hours are up' 2>/dev/null || true" \
		"exit 0" || die "staging failed; $id is still up (build_release.sh stop)"
	ssm_output | grep -E '^(game|on the GPU):|Autocraft|rror' | head -40 || true
	mkdir -p "$releases/$version"
	s3 cp --only-show-errors "$shot" "$releases/$version/linux-home.png" && say "the home screen: $releases/$version/linux-home.png"
	say "ready: $id has $version on its desktop; it is terminated in $hours h (build_release.sh stop ends it sooner)"
	cmd_dcv "$id"
}

cmd_dcv() {
	local id=${1:-$(running tester)}
	[ "$id" = None ] && die "no tester running: build_release.sh test"
	local pw user=Administrator; pw=$(password "$id")
	is_linux "$id" && user=ubuntu
	echo
	echo "  Open https://localhost:18443 in a browser (accept its own certificate),"
	echo "  or the Amazon DCV client with localhost:18443."
	echo "    User:     $user"
	echo "    Password: $pw"
	echo
	echo "  The tunnel stays open while this runs; Ctrl-C closes it (the machine runs on)."
	echo
	aws ssm start-session --target "$id" --document-name AWS-StartPortForwardingSession \
		--parameters 'portNumber=["8443"],localPortNumber=["18443"]'
}

# ---------------------------------------------------------------- upkeep

cmd_status() {
	echo "Machines:"
	aws ec2 describe-instances --filters Name=tag:Project,Values=ringshadow \
		Name=instance-state-name,Values=pending,running,stopping,stopped \
		--query 'Reservations[].Instances[].[InstanceId,InstanceType,State.Name,Tags[?Key==`Role`]|[0].Value,LaunchTime]' --output text
	echo "Images:"
	aws ec2 describe-images --owners self --filters Name=tag:Project,Values=ringshadow \
		--query 'sort_by(Images,&CreationDate)[].[ImageId,Name,State,CreationDate]' --output text
	echo "Builds in s3://$(bucket)/builds/:"
	s3 ls "s3://$(bucket)/builds/" 2>/dev/null || true
}

cmd_stop() {
	local ids
	ids=$(aws ec2 describe-instances --filters Name=tag:Project,Values=ringshadow \
		Name=instance-state-name,Values=pending,running,stopping,stopped \
		--query 'Reservations[].Instances[].InstanceId' --output text)
	[ -z "$ids" ] && { say "no Ringshadow machines running"; return 0; }
	# shellcheck disable=SC2086
	aws ec2 terminate-instances --instance-ids $ids --query 'TerminatingInstances[].[InstanceId,CurrentState.Name]' --output text
}

if [ "${1:-}" = --region ]; then
	[ -n "${2:-}" ] || die "--region needs a region"
	export AWS_REGION=$2; shift 2
fi
case ${1:-} in
	infra|setup|rdp|image|build|mac|sign|test|dcv|status|stop) c=$1; shift; "cmd_$c" "$@" ;;
	*) sed -n '2,33p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
