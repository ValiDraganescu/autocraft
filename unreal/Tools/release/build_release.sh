#!/bin/bash
# Release builds of Ringshadow (docs/builds.md). Windows and Linux are built on
# a Windows EC2 machine made on demand from a saved image (AMI); the Mac build
# is made here. Everything in AWS carries the tag Project=ringshadow.
#
#   build_release.sh infra                  bucket, instance role, security group, key pair (once; rerunnable)
#   build_release.sh setup                  start the setup machine from Windows Server and install the build tools
#   build_release.sh rdp [INSTANCE]         remote desktop to it through SSM: localhost:13389, Administrator's password
#   build_release.sh image [INSTANCE]       save the setup machine as the build image, then terminate it
#   build_release.sh build [REF] [options]  build REF (default HEAD) on a spot machine made from the newest image
#       --version V        name of the zips (default: git describe of REF)
#       --platforms LIST   Win64,Linux (default) or one of them
#       --config C         Shipping (default) or Development
#       --instance ID      build on a running machine (the setup one, to warm it) instead of a new one
#       --on-demand        an on-demand machine instead of spot
#       --keep             leave the machine running afterwards (stop it with `stop`)
#   build_release.sh mac [--version V] [--config C]   package the Mac build on this Mac
#   build_release.sh status                 machines, images and builds in AWS
#   build_release.sh stop                   terminate every running Ringshadow build machine
#
# The builds land in unreal/Saved/Releases/<version>/ (not in git).
# AUTOCRAFT_AWS_PROFILE picks the AWS profile (default: ringshadow).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
releases="$repo/unreal/Saved/Releases"

export AWS_PROFILE=${AUTOCRAFT_AWS_PROFILE:-ringshadow}
# The organization's policy allows EC2 only in Stockholm.
export AWS_REGION=eu-north-1
export AWS_PAGER=""

role=ringshadow-build
group=ringshadow-build
key=ringshadow-build
keyfile="$HOME/.ssh/ringshadow-build.pem"
# 32 vCPU, the whole standard-instance quota, for setup too: its first
# build compiles every shader into the cache the image keeps.
build_type=c7i.8xlarge
disk_gb=400

say() { printf '%s %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { printf 'build_release: %s\n' "$*" >&2; exit 1; }

account() { aws sts get-caller-identity --query Account --output text; }
bucket() { echo "ringshadow-builds-$(account)"; }

group_id() {
	aws ec2 describe-security-groups --filters "Name=group-name,Values=$group" \
		--query 'SecurityGroups[0].GroupId' --output text
}

# A JSON string for one line of PowerShell.
json() { printf '"%s"' "$(printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')"; }

# ---------------------------------------------------------------- infra

cmd_infra() {
	local b; b=$(bucket)
	if ! aws s3api head-bucket --bucket "$b" 2>/dev/null; then
		say "bucket $b"
		aws s3api create-bucket --bucket "$b" --create-bucket-configuration "LocationConstraint=$AWS_REGION" >/dev/null
		aws s3api put-public-access-block --bucket "$b" --public-access-block-configuration \
			BlockPublicAcls=true,IgnorePublicAcls=true,BlockPublicPolicy=true,RestrictPublicBuckets=true
		# Builds are kept on the Mac and on GitHub; the bucket only carries them.
		aws s3api put-bucket-lifecycle-configuration --bucket "$b" --lifecycle-configuration \
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

	# The key only decrypts the Windows Administrator password.
	if ! aws ec2 describe-key-pairs --key-names "$key" >/dev/null 2>&1; then
		[ -e "$keyfile" ] && die "$keyfile exists but AWS has no key pair $key"
		say "key pair $key -> $keyfile"
		mkdir -p "$(dirname "$keyfile")"
		( umask 077; aws ec2 create-key-pair --key-name "$key" --query KeyMaterial --output text >"$keyfile" )
	fi
	say "infra ready: bucket $b, role $role, group $group, key $keyfile"
}

# ---------------------------------------------------------------- machines

launch() {  # launch AMI TYPE ROLE NAME MARKET: prints the instance id
	local spot=()
	[ "$5" = spot ] && spot=(--instance-market-options 'MarketType=spot,SpotOptions={SpotInstanceType=one-time,InstanceInterruptionBehavior=terminate}')
	local t="{Key=Project,Value=ringshadow},{Key=Role,Value=$3},{Key=Name,Value=$4}"
	aws ec2 run-instances --image-id "$1" --instance-type "$2" --count 1 \
		--key-name "$key" --security-group-ids "$(group_id)" \
		--iam-instance-profile "Name=$role" \
		--block-device-mappings "DeviceName=/dev/sda1,Ebs={VolumeSize=$disk_gb,VolumeType=gp3,Iops=6000,Throughput=500,DeleteOnTermination=true}" \
		--metadata-options HttpTokens=required \
		--tag-specifications "ResourceType=instance,Tags=[$t]" "ResourceType=volume,Tags=[$t]" \
		${spot[@]+"${spot[@]}"} \
		--query 'Instances[0].InstanceId' --output text
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
run_ps() {
	local id=$1 timeout=$2 label=$3; shift 3
	local b lines="" l
	b=$(bucket)
	for l in "$@"; do lines="$lines${lines:+,}$(json "$l")"; done
	local cmd
	cmd=$(aws ssm send-command --instance-ids "$id" --document-name AWS-RunPowerShellScript \
		--comment "ringshadow $label" --timeout-seconds 600 \
		--output-s3-bucket-name "$b" --output-s3-key-prefix ssm \
		--parameters "{\"commands\":[$lines],\"executionTimeout\":[\"$timeout\"]}" \
		--query Command.CommandId --output text)
	say "$label: SSM command $cmd"
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
	local out="s3://$b/ssm/$cmd/$id/awsrunPowerShellScript/0.awsrunPowerShellScript"
	for f in stdout stderr; do
		echo "--- $f (last 40 lines; whole: $out/$f)"
		aws s3 cp --only-show-errors "$out/$f" - 2>/dev/null | tail -40 || true
	done
	return 1
}

running_setup() {
	aws ec2 describe-instances --filters Name=tag:Project,Values=ringshadow Name=tag:Role,Values=setup \
		Name=instance-state-name,Values=pending,running,stopping,stopped \
		--query 'Reservations[0].Instances[0].InstanceId' --output text
}

cmd_setup() {
	cmd_infra
	local existing; existing=$(running_setup)
	[ "$existing" != None ] && die "a setup machine is already there: $existing (rdp, image or stop it)"
	local ami
	ami=$(aws ssm get-parameter --name /aws/service/ami-windows-latest/Windows_Server-2022-English-Full-Base \
		--query Parameter.Value --output text)
	say "launching the setup machine ($build_type, Windows Server 2022, $ami)"
	local id; id=$(launch "$ami" "$build_type" setup ringshadow-build-setup on-demand)
	say "setup machine $id"
	wait_ssm "$id"
	aws s3 cp --only-show-errors "$here/setup_windows.ps1" "s3://$(bucket)/scripts/setup_windows.ps1"
	# The base image has the AWS Tools for PowerShell but not the CLI yet.
	run_ps "$id" 7200 setup \
		"\$ErrorActionPreference = 'Stop'" \
		"New-Item -ItemType Directory -Force C:\\build\\scripts | Out-Null" \
		"Read-S3Object -BucketName '$(bucket)' -Key scripts/setup_windows.ps1 -File C:\\build\\scripts\\setup_windows.ps1 -Region $AWS_REGION | Out-Null" \
		"& C:\\build\\scripts\\setup_windows.ps1" \
		"exit 0"
	say "next: build_release.sh rdp, then the two installers on its desktop (docs/builds.md)"
}

cmd_rdp() {
	local id=${1:-$(running_setup)}
	[ "$id" = None ] && die "no setup machine; give an instance id"
	[ -r "$keyfile" ] || die "no $keyfile"
	local pw=""
	while [ -z "$pw" ]; do
		pw=$(aws ec2 get-password-data --instance-id "$id" --priv-launch-key "$keyfile" --query PasswordData --output text)
		[ -z "$pw" ] && { say "the password is not ready yet (Windows makes it in the first minutes)"; sleep 20; }
	done
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

newest_image() {
	aws ec2 describe-images --owners self --filters Name=tag:Project,Values=ringshadow Name=state,Values=available \
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

	aws s3 cp --only-show-errors "$here/build_windows.ps1" "s3://$b/scripts/build_windows.ps1"
	run_ps "$id" 14400 build \
		"\$ErrorActionPreference = 'Stop'" \
		"& 'C:\\Program Files\\Amazon\\AWSCLIV2\\aws.exe' s3 cp --only-show-errors s3://$b/scripts/build_windows.ps1 C:\\build\\scripts\\build_windows.ps1" \
		"& C:\\build\\scripts\\build_windows.ps1 -Ref $sha -Version '$version' -Bucket '$b' -Platforms '$platforms' -Config $config" \
		"exit 0" \
		|| { say "build log: s3://$b/builds/$version/build.log"; exit 1; }

	local dir="$releases/$version"
	mkdir -p "$dir"
	aws s3 cp --only-show-errors --recursive "s3://$b/builds/$version/" "$dir/"
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
	local dir="$releases/$version" uat="/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/RunUAT.sh"
	local archive="$dir/mac"
	rm -rf "$archive"
	mkdir -p "$dir"
	say "packaging the Mac build $version ($config) from the working tree"
	"$uat" BuildCookRun -project="$repo/unreal/Autocraft.uproject" -target=Autocraft -platform=Mac \
		-clientconfig="$config" -build -cook -stage -pak -iostore -compressed \
		-archive -archivedirectory="$archive" -nodebuginfo -utf8output -unattended -nop4 \
		>"$dir/mac-build.log" 2>&1 || die "UAT failed: $dir/mac-build.log"
	local app; app=$(find "$archive" -maxdepth 2 -name '*.app' -type d | head -1)
	[ -n "$app" ] || die "no .app in $archive"
	ditto -c -k --keepParent "$app" "$dir/Ringshadow-$version-mac.zip"
	say "done: $dir/Ringshadow-$version-mac.zip (unsigned: docs/builds.md, Signing the Mac build)"
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
	aws s3 ls "s3://$(bucket)/builds/" 2>/dev/null || true
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

case ${1:-} in
	infra|setup|rdp|image|build|mac|status|stop) c=$1; shift; "cmd_$c" "$@" ;;
	*) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
