#!/bin/bash
# The Linux tester's desktop (docs/builds.md), run as root through SSM by
# `build_release.sh test --linux` on AWS's Ubuntu 24.04 GPU image, which has
# the NVIDIA driver (its X driver and Vulkan too) but no X server: Xorg on
# the GPU with a 1920x1080 screen and no monitor, Xfce logged in as ubuntu,
# and Amazon DCV sharing that screen (https on 8443, through SSM).
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq --no-install-recommends \
	xserver-xorg-core xserver-xorg-input-libinput xinit xfce4 xfce4-terminal \
	lightdm lightdm-gtk-greeter dbus-x11 x11-utils x11-xserver-utils imagemagick \
	libvulkan1 vulkan-tools >/var/log/ringshadow-apt.log 2>&1

# No monitor: an empty start and a virtual screen.
nvidia-xconfig --preserve-busid --enable-all-gpus --allow-empty-initial-configuration --virtual=1920x1080 >/dev/null
mkdir -p /etc/lightdm/lightdm.conf.d
printf '[Seat:*]\nautologin-user=ubuntu\nautologin-session=xfce\nuser-session=xfce\n' \
	>/etc/lightdm/lightdm.conf.d/50-ringshadow.conf
groupadd -f autologin
usermod -aG autologin ubuntu
# The output comes up at 1024x768; the session sets 1920x1080 when it starts.
cat >/usr/local/bin/ringshadow-screen <<'EOF'
#!/bin/sh
xrandr --output "$(xrandr | awk '/ connected/{print $1; exit}')" --mode 1920x1080
EOF
chmod 755 /usr/local/bin/ringshadow-screen
mkdir -p /home/ubuntu/.config/autostart
printf '[Desktop Entry]\nType=Application\nName=Screen size\nExec=/usr/local/bin/ringshadow-screen\n' \
	>/home/ubuntu/.config/autostart/screen-size.desktop
chown -R ubuntu:ubuntu /home/ubuntu/.config
systemctl set-default graphical.target >/dev/null
systemctl restart lightdm

# Amazon DCV, from Amazon's own download, its package signed with its key.
cd /tmp
curl -fsSLO https://d1uj6qtbmh3dt5.cloudfront.net/NICE-GPG-KEY
gpg --import NICE-GPG-KEY 2>/dev/null
curl -fsSL -o dcv.tgz https://d1uj6qtbmh3dt5.cloudfront.net/nice-dcv-ubuntu2404-x86_64.tgz
rm -rf /tmp/dcv
mkdir /tmp/dcv
tar -xzf dcv.tgz -C /tmp/dcv --strip-components=1
apt-get install -y -qq /tmp/dcv/nice-dcv-server_*.deb /tmp/dcv/nice-dcv-web-viewer_*.deb >>/var/log/ringshadow-apt.log 2>&1
usermod -aG video dcv
# The console session (the GPU's own screen), made at start, owned by ubuntu.
sed -i 's/^#\{0,1\}create-session *=.*/create-session = true/' /etc/dcv/dcv.conf
sed -i 's/^#\{0,1\}owner *=.*/owner = "ubuntu"/' /etc/dcv/dcv.conf
systemctl enable dcvserver >/dev/null 2>&1
systemctl restart dcvserver

sleep 10
export DISPLAY=:0 XAUTHORITY=/home/ubuntu/.Xauthority
sudo -u ubuntu -E /usr/local/bin/ringshadow-screen
echo "GPU: $(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader)"
echo "Vulkan: $(sudo -u ubuntu -E vulkaninfo --summary 2>/dev/null | awk -F'= ' '/deviceName/{print $2; exit}')"
echo "DCV: $(dcv list-sessions)"
