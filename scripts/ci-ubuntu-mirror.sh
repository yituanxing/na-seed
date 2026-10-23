#!/bin/sh
# Signed Ubuntu archive selection for GitHub-hosted runner DEB822 mirror+file
# sources. Azure's mirror intermittently stalls QEMU installs; preserve apt
# signature checks and package selections instead of disabling verification.
set -eu
for file in /etc/apt/apt-mirrors.txt /etc/apt/sources.list /etc/apt/sources.list.d/ubuntu.sources; do
    [ -f "$file" ] || continue
    sudo sed -i 's|http://azure.archive.ubuntu.com/ubuntu|http://archive.ubuntu.com/ubuntu|g' "$file"
done
