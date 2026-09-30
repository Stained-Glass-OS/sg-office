#!/bin/sh
# Make the Debian trixie build root SG Office's engine builds in: rootless
# (mmdebstrap --mode=unshare, needs /etc/subuid), read-only once made.
#   build/mkroot.sh [ROOT]          (default /var/tmp/sgoffice/root-build)
set -eu
ROOT=${1:-/var/tmp/sgoffice/root-build}
PKGS="build-essential git curl ca-certificates python3 python-is-python3 pkg-config cmake ninja-build
xz-utils file unzip p7zip-full wget lsb-release qtbase5-dev qtbase5-dev-tools
libboost-all-dev libicu-dev libcurl4-openssl-dev libssl-dev libxml2-dev libglib2.0-dev zlib1g-dev
libnode-dev nodejs npm libharfbuzz-dev libhunspell-dev libfreetype-dev libfontconfig-dev
libgtk-3-dev libx11-dev libxkbcommon-dev"
[ ! -e "$ROOT" ] || { echo "$ROOT exists"; exit 1; }
nice -n 10 mmdebstrap --mode=unshare --variant=apt --format=directory \
    --include="$(echo $PKGS | tr ' ' ,)" trixie "$ROOT" http://deb.debian.org/debian
