#!/bin/sh
# Run SG Office headlessly for a test: inside the build root, on its own
# Xvfb server (the sandbox's /tmp is private: no other display is reachable),
# with a scratch HOME. Chromium's own sandbox is off here only because bwrap
# already confines the run.
#
#   test/app/run.sh ROOT WORKDIR [sg-office args...]     (env: SG_OFFICE_AUTOPILOT, ...)
#   SG_OFFICE_SYSROOT=DIR: run /usr/bin/sg-office of that system root instead of ROOT/bin
set -eu
HERE=$(cd "$(dirname "$0")/../.." && pwd)
ROOT=$1; WORK=$2; shift 2
mkdir -p "$WORK/home"
export HOME="$WORK/home" XDG_CACHE_HOME="$WORK/home/.cache" XDG_CONFIG_HOME="$WORK/home/.config" \
       XDG_DATA_HOME="$WORK/home/.local/share" XDG_RUNTIME_DIR="$WORK/run"
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
export SG_OFFICE_LOG=${SG_OFFICE_LOG:-1}
if [ -n "${SG_OFFICE_SYSROOT:-}" ]; then
    # as installed (test/deb): a system root with the package in it, the
    # program at /usr/bin finding everything at its own default paths
    export SG_ROOT="$SG_OFFICE_SYSROOT"; PROG=/usr/bin/sg-office
    unset SG_OFFICE_ENGINE SG_OFFICE_SHARE
else
    export SG_OFFICE_ENGINE="$ROOT/lib/sg-office/engine" SG_OFFICE_SHARE="$ROOT/share/sg-office"; PROG=$ROOT/bin/sg-office
fi
# SG_TEST_LC_ALL: another locale (the units gate: en_US measures in inches)
export LANG=C.UTF-8 LC_ALL=${SG_TEST_LC_ALL:-C.UTF-8}
export QTWEBENGINE_DISABLE_SANDBOX=1 QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu" QT_QPA_PLATFORM=xcb
unset DISPLAY WAYLAND_DISPLAY
exec env SG_CWD="$WORK" sh "$HERE/build/inroot.sh" timeout "${SG_TIMEOUT:-180}" \
    xvfb-run -a -s "-screen 0 1600x1000x24" "$PROG" "$@"
