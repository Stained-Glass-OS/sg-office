#!/bin/sh
# Run a command in the build root (read-only) as the calling user, with the
# work area writable. No privileges: bwrap.
#   SG_ROOT=... SG_AREA=... build/inroot.sh CMD...
ROOT=${SG_ROOT:-/var/tmp/sgoffice/root-build}
AREA=${SG_AREA:-/var/tmp/sgoffice}
mkdir -p "$AREA/home"
exec bwrap --ro-bind "$ROOT" / --tmpfs /var/tmp --bind "$AREA" "$AREA" \
  --dev /dev --proc /proc --tmpfs /tmp --tmpfs /run \
  --ro-bind /etc/resolv.conf /etc/resolv.conf \
  --setenv HOME "$AREA/home" --setenv PATH /usr/local/bin:/usr/bin:/bin \
  --chdir "${SG_CWD:-$PWD}" "$@"
