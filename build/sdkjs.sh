#!/bin/sh
# Build SG Office's document engine (ONLYOFFICE sdkjs + our patches) from
# source with node + Google Closure Compiler (native binary from npm; no Java,
# no Microsoft tools). Output: $OUT/sdkjs/{word,cell,slide,common,...}.
#
#   build/sdkjs.sh [WORK] [OUT]      (defaults /var/tmp/sgoffice/{src,out})
#
# SG_NPM: an npm to use when the host has none (a self-contained npm-cli.js).
# SG_SDKJS_NOPATCH=1 builds upstream unpatched (used by the mutation test).
# SG_SDKJS_EDITORS: grunt tasks to run (default: the whole SDK).
# SG_SDKJS_DESKTOP=1 builds the desktop variant (--desktop=true: the editors'
# local-file bridge, sdkjs/*/Local) into $OUT/sdkjs-desktop, which the SG
# Office app runs; the plain build ($OUT/sdkjs) is what docbuilder/x2t run.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
. "$HERE/upstream.conf"
WORK=${1:-/var/tmp/sgoffice/src}
OUT=${2:-/var/tmp/sgoffice/out}
NPM=${SG_NPM:-npm}
SRC=$WORK/sdkjs
mkdir -p "$WORK" "$OUT"

if [ ! -d "$SRC/.git" ]; then
    git clone -q --depth 1 --branch "$SDKJS_TAG" "$SDKJS_URL" "$SRC"
fi
git -C "$SRC" cat-file -e "$SDKJS_COMMIT^{commit}" 2>/dev/null || { echo "sdkjs: pinned commit $SDKJS_COMMIT not found"; exit 1; }

# a clean tree, then our series
git -C "$SRC" checkout -q -f --detach "$SDKJS_COMMIT"
git -C "$SRC" clean -qfdx -e build/node_modules
if [ "${SG_SDKJS_NOPATCH:-0}" != 1 ]; then
    while read -r p; do
        case "$p" in ''|'#'*) continue;; esac
        git -C "$SRC" apply --whitespace=nowarn "$HERE/patches/sdkjs/$p"
        echo "applied $p"
    done < "$HERE/patches/sdkjs/series"
fi

cd "$SRC/build"
[ -d node_modules ] || nice -n 10 $NPM ci --no-audit --no-fund
DEST=$OUT/sdkjs; FLAGS=
if [ "${SG_SDKJS_DESKTOP:-0}" = 1 ]; then DEST=$OUT/sdkjs-desktop; FLAGS=--desktop=true; fi
nice -n 10 node node_modules/grunt/bin/grunt --no-color $FLAGS ${SG_SDKJS_EDITORS:-}

rm -rf "$DEST"
cp -a "$SRC/deploy/sdkjs" "$DEST"
if [ "${SG_SDKJS_NOPATCH:-0}" = 1 ]; then n="UNPATCHED"; else n="+ $(grep -cv '^#' "$HERE/patches/sdkjs/series" || true) patch(es)"; fi
echo "sdkjs built: $DEST ($(git -C "$SRC" describe --tags) $n)"
