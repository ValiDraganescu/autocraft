# Release builds

How Ringshadow is packaged for players: Windows and Linux on a Windows
machine in AWS, made on demand, and the Mac build on this Mac. The script is
`unreal/Tools/release/build_release.sh`. Run it with no arguments for its
commands.

Status, 2026-10-08: the build machine's image is `ringshadow-build-20261008-1342`
(`ami-0fed5dee9c6ddb99a`), warmed on the setup machine: the first full Win64
and Linux build (`138d96b`) took 12 minutes, and a rebuild of `7737714`, which changed
only the build scripts, took 2 on the same machine. The first release from the
image (`a042937`) took 28 minutes from launch to terminate, 23 of them on
the build: that commit made five plugins editor-only, which recompiles the
game for both platforms, and the image's cache is older than it. Refresh the
image ("Refreshing the image") to bring the cache up to date. Nobody has
played those packages yet. "Files outside the cook" lists what the game reads
outside the cook and how it is staged.

## What is built where

| Platform | Built on | Package |
|---|---|---|
| Windows (x64) | Windows EC2 machine, UE 5.8 from the Epic Games Launcher | `Ringshadow-<version>-windows.zip` |
| Linux (x64) | The same machine, cross-compiled with Epic's toolchain | `Ringshadow-<version>-linux.tar.gz` |
| macOS (Apple silicon) | This Mac | `Ringshadow-<version>-mac.zip` |

One Linux build runs on Ubuntu, Omarchy (Arch) and the other mainstream
distributions: Unreal's Linux target carries its own C++ runtime and needs
glibc and a Vulkan driver. Unreal cannot build Windows or Linux on a Mac, and
an x86 VM on Apple silicon is too slow for a cook, so those two need a real
x86 machine.

The packages land in `unreal/Saved/Releases/<version>/` (ignored by git), with
`SHA256SUMS`. The script uploads nothing to GitHub. Publishing a release is a
separate step.

## Files outside the cook

A cooked game reads only what is cooked or staged. The game reads some plain
files from the project folder itself; `Autocraft.Build.cs` stages them loose,
at the same paths, for the game targets (`RuntimeDependencies`, `NonUFS`):
- the music, `Resources/Sounds/music/*.mp3` (`AcMusicPlayer.cpp`);
- the launcher art, `Content-src/launcher/*.jpg` (`AcLauncherArt.cpp`);
- `Content/Audio/Sounds.json` and `Content/Models/ModelCatalog.json`;
- the cursors, `Content/UI/Cursors`. macOS loads the TIFFs. Windows and
  Linux load `Cursor_<kind>.png` and `@2x.png` instead
  (`UGameViewportClient::LoadCursorFromPngs`). `Tools/cursors/bake_cursors.sh`
  writes both.

A new file the game reads at run time goes on that list too. `Shatter` is
staged inside the pak by `DirectoriesToAlwaysStageAsUFS` in
`DefaultGame.ini`.

The editor's plugins (`ModelContextProtocol`, `AllToolsets`,
`PythonScriptPlugin`, `EditorScriptingUtilities`, `USDImporter`) have
`"TargetAllowList": ["Editor"]` in `Autocraft.uproject`, so a player's game
carries no MCP server and no Python.

The console's art (the dashboard, the cab's frame, the plates) is drawn
the same way on every platform since 2026-10-08: Slate geometry baked per
window size (`AcCabArt.cpp`, `AcArtList.h`, `AcBakedArt.h`). It was Core
Graphics on the Mac and flat colour elsewhere before.

Already fine: the saves (`AcSaves.cpp` uses the platform's settings folder off
the Mac) and the Mac-only code (icon, watchdog, GPU timing), which is behind
`PLATFORM_MAC`. The executable keeps the project's name, `Autocraft`
(AGENTS.md). The folder in each package is called `Ringshadow`.

The cheapest way to find what else is missing is `build_release.sh mac`: a
Mac package built here, played by hand, before any AWS time is spent.

## AWS

- **Account** 569854554192, CLI profile `ringshadow` (`aws login --profile
  ringshadow`; a session lasts about 12 hours). The script uses it by default.
  `AUTOCRAFT_AWS_PROFILE` picks another.
- **Region** `eu-north-1` (Stockholm), the only one the organization's policy
  allows for EC2. The same policy blocks Trusted Advisor and Compute Optimizer
  in the console, and every signed S3 call outside eu-north-1 (even to a
  public bucket). The rule is `UsEast1Partitional` in the SCP
  AdvancedModeRegionRestrictionSecurityControlPolicy (`p-lunneq4k`), which
  only the management account (687971795322) can edit. The tester's driver
  bucket in us-east-1 is public, so it is read without credentials, which no
  policy sees ("The Windows tester").
- **Quotas** (2026-10-08): 32 vCPU of standard instances, on-demand and spot.
  That is one `c7i.8xlarge`, so the setup machine and a build cannot run at
  the same time. GPU (G and VT) instances: 8 vCPU, on-demand and spot
  (granted 2026-10-08), one `g6.2xlarge`.
- **What the script makes** (`infra`; every resource is tagged
  `Project=ringshadow`; the tester's machines and image too):
  - Bucket `ringshadow-builds-569854554192`: private, objects expire after 60
    days.
  - Role and instance profile `ringshadow-build`: SSM, plus read and write on
    the bucket.
  - Security group `ringshadow-build`: no inbound rules. SSM and remote
    desktop both run over SSM's outbound connection, so no port is open.
  - Key pair `ringshadow-build` (`~/.ssh/ringshadow-build.pem`): used only to
    decrypt the Windows Administrator password.

### Costs

Spot prices in `eu-north-1` on 2026-10-08:

| What | Price |
|---|---|
| `c7i.8xlarge` Windows, spot | $1.62 an hour. About $1.47 of that is the Windows licence, which spot does not discount. |
| The same, on-demand | About $2.90 an hour. The setup machine runs on-demand. |
| The 400 GB gp3 disk at 6000 IOPS and 500 MB/s | Cents an hour while a machine runs. |
| The image's snapshot | About $0.05 a GB-month on the used blocks: 111 GB, about $5.60 a month. |
| A release (Windows and Linux) | $0.75 for `a042937`: 28 minutes of a spot machine, a full recompile. A release that recompiles less costs less. |
| A month at 2 to 3 releases a week | At most $7–10 of machine time, $5.60 for the image and under $1 for S3 and the downloads: about $15. |
| The one-time setup | The setup machine ran 3 hours on demand on 2026-10-08, the Windows build fixes included: about $9. |
| `g6.2xlarge` (NVIDIA L4) for playtesting, Windows, spot | $0.47 an hour. |

The times come from CloudTrail (`RunInstances`, `TerminateInstances`) and
each build's `build.log`; the snapshot's size from `aws ec2
describe-snapshots` (`FullSnapshotSizeInBytes`).

## One-time setup

1. **`build_release.sh setup`** runs `infra`, then starts an on-demand
   `c7i.8xlarge` from the newest Windows Server 2022 image, with a 400 GB
   disk. Through SSM it runs `setup_windows.ps1`, which installs:
   - Visual Studio 2022 Build Tools: the C++ workload, MSVC 14.44 (17.14) and
     Windows SDK 10.0.22621, the versions in
     `Engine/Config/Windows/Windows_SDK.json`.
   - Git and the AWS CLI.

   The MSVC folder is named `14.44.35207`, inside the 14.44.0–14.44.35210
   range the engine bans. That is fine: Visual Studio services the compiler
   in place (`cl.exe` was 19.44.35229 on 2026-10-08), and UBT reads the
   version from `cl.exe`, not from the folder name.

   It also clones the repo to `C:\build\autocraft`, leaves `C:\build` and the
   engine out of Defender's scans, and puts two installers on the desktop.
   It takes about 10 minutes from the launch. The Visual Studio installer
   keeps working in the background for a few minutes after the script ends.
2. **`build_release.sh rdp`** prints the Administrator password and opens a
   tunnel. Connect Windows App (Mac App Store) to `localhost:13389`. On the
   machine:
   1. Run `EpicGamesLauncherInstaller.msi` and sign in to the launcher.
   2. Install **Unreal Engine 5.8.3**, the same version as on the Mac, at the
      default path (`C:\Program Files\Epic Games\UE_5.8`). In its Options,
      tick the **Linux** target platform. Untick Starter Content, Templates
      and the editor debug symbols, which are large and not needed.
   3. Run `v26_clang-20.1.8-rockylinux8.exe`, the Linux cross toolchain named
      in `Engine/Config/Linux/Linux_SDK.json`. It sets `LINUX_MULTIARCH_ROOT`.
   If **Downloads** ends with no `Engine\Intermediate\Build\Linux` in the
   engine folder, the Linux tick did not take: the Library tab's engine tile,
   ▾ next to its Launch, Options, tick Linux, Apply. Then sign out of the
   launcher, so the image keeps no Epic session.
3. **Warm build:** `build_release.sh build --instance <setup id>`. This is the
   first package. It compiles every shader for both platforms into the
   machine's derived data cache, which the image keeps, so later builds skip
   that work. This is also where packaging problems show up first. The setup
   machine is still there to look at them over remote desktop.
4. **`build_release.sh image`** saves the setup machine as an image
   (`ringshadow-build-<date>`), waits until it is ready (about 15 minutes for
   the 400 GB disk on 2026-10-08), then terminates the machine.

## A release

```sh
git push                                     # the machine clones from GitHub
unreal/Tools/release/build_release.sh build  # HEAD; or a tag, a branch, a commit
unreal/Tools/release/build_release.sh mac    # the Mac package, here
```

`build`:
1. Checks that the commit is on GitHub.
2. Starts a spot `c7i.8xlarge` from the newest image and waits for SSM.
3. Runs `build_windows.ps1`: fetch, check out the commit, then
   `RunUAT BuildCookRun` for Win64 and Linux (Shipping, pak, IoStore,
   compressed, no debug files; the Windows package includes the
   prerequisites installer).
4. Zips each package and uploads the zips, a `manifest.txt` and `build.log`
   to `s3://ringshadow-builds-569854554192/builds/<version>/`.
5. Downloads them to `unreal/Saved/Releases/<version>/` and sets the
   executable bits that a tar made on Windows cannot carry.
6. Terminates the machine, on failure too.

Options:
- `--version`: name the packages (default: `git describe` of the commit).
- `--platforms Win64` or `--platforms Linux`: build only one.
- `--config Development`: a build with logs and the console.
- `--on-demand`: if spot capacity is short.
- `--keep`: leave the machine running to look at it (`rdp <id>`, then `stop`).

If AWS reclaims a spot machine mid-build, the script stops with "spot
reclaimed?". Run it again.

`status` lists the machines, images and builds. `stop` terminates every
Ringshadow machine.

### Refreshing the image

After an engine update, or when builds get slow because the cache in the
image has fallen behind:
1. `build --keep`.
2. If the engine changed: `rdp <id>` and update it in the launcher.
3. `image <id>`.

Then delete the older image and its snapshot:

```sh
aws ec2 deregister-image --image-id ami-…
aws ec2 delete-snapshot --snapshot-id snap-…
```

`status` lists the images.

## Signing the Mac build

`build_release.sh mac` makes an unsigned `.app`. Gatekeeper stops an unsigned
download, and the player has to right-click and choose Open. A clean first
launch needs:
- a **Developer ID Application** certificate (Apple Developer Program,
  $99 a year; this Mac has only an Apple Development one);
- `codesign --deep --options runtime` with it;
- `xcrun notarytool submit --wait`, then `xcrun stapler staple`.

Not scripted yet.

## Testing the packages

The build machine has no GPU. A package is proven when someone plays it:
- **Windows in AWS:** `build_release.sh test [VERSION]`, below.
- **Linux in AWS:** not scripted yet (Ubuntu, the NVIDIA driver, DCV).
- **Linux on real hardware:** a player on Ubuntu or Omarchy, by hand.

### The Windows tester

```sh
unreal/Tools/release/build_release.sh test            # the newest Windows package in the bucket
unreal/Tools/release/build_release.sh test a8a6b03    # or one by name
unreal/Tools/release/build_release.sh dcv             # the remote desktop again, after Ctrl-C
unreal/Tools/release/build_release.sh stop            # done
```

`test` starts a spot `g6.2xlarge` (NVIDIA L4, 24 GB: DirectX 12 and Vulkan;
about $0.47 an hour), unpacks the package to `C:\Ringshadow`, runs its
prerequisites installer, puts a shortcut on the desktop and opens a tunnel to
Amazon DCV, a remote desktop that streams what the GPU draws (plain remote
desktop cannot show a 3D game well). Open `https://localhost:18443` in a
browser, accept the machine's own certificate and sign in as Administrator
with the password it prints. The machine switches itself off and is
terminated after 4 hours (`--hours H`), and `stop` ends it sooner.

The first `test` sets the tester up from Windows Server 2022 with
`setup_tester.ps1` (the AWS CLI, NVIDIA's GRID driver, which AWS licenses
for its G instances, and DCV), reboots it and saves it as the image
`ringshadow-tester-<date>`. Later ones start from that image.

The driver comes from AWS's public bucket `ec2-windows-nvidia-drivers` in
us-east-1, over plain HTTPS without credentials: a signed read would meet the
organization's region policy (see AWS above).
