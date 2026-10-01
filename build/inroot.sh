#!/bin/sh
# Run a command in the build root (read-only) as the calling user, with the
# work area writable. No privileges: bwrap.
#   SG_ROOT=... SG_AREA=... build/inroot.sh CMD...
# A checkout outside the work area (a release worktree) is seen at
# /var/tmp/sg-office-src inside, and SG_CWD under it is translated there.
ROOT=${SG_ROOT:-/var/tmp/sgoffice/root-build}
AREA=${SG_AREA:-/var/tmp/sgoffice}
SRC=$(cd "$(dirname "$0")/.." && pwd)
CWD=${SG_CWD:-$PWD}
mkdir -p "$AREA/home"
case "$SRC/" in
    "$AREA"/*) MAP= ;;
    *) MAP="--bind $SRC /var/tmp/sg-office-src"
       case "$CWD/" in "$SRC"/*) CWD=/var/tmp/sg-office-src${CWD#"$SRC"} ;; esac ;;
esac
exec bwrap --ro-bind "$ROOT" / --tmpfs /var/tmp --bind "$AREA" "$AREA" $MAP \
  --dev /dev --proc /proc --tmpfs /tmp --tmpfs /run \
  --ro-bind /etc/resolv.conf /etc/resolv.conf \
  --setenv HOME "$AREA/home" --setenv PATH /usr/local/bin:/usr/bin:/bin \
  --chdir "$CWD" "$@"
