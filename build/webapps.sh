#!/bin/sh
# Build the editors' interface (ONLYOFFICE web-apps + our patches) from source
# with node and grunt: the Documents, Spreadsheets and Presentations editors
# and the api.js that hosts them. Output: $OUT/web-apps.
#
#   build/webapps.sh [WORK] [OUT]    (defaults /var/tmp/sgoffice/{src,out})
#
# npm runs with --ignore-scripts and without dev dependencies: no package may
# download and run a prebuilt binary (grunt-contrib-imagemin's image
# compressors, grunt-mocha's browser). Images are therefore copied, not
# optimised (--sg-copy-images, patches/web-apps/0001); --force as upstream's
# build_tools runs this build.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
. "$HERE/upstream.conf"
WORK=${1:-/var/tmp/sgoffice/src}
OUT=${2:-/var/tmp/sgoffice/out}
NPM=${SG_NPM:-npm}
SRC=$WORK/web-apps
mkdir -p "$WORK" "$OUT"
[ -d "$SRC/.git" ] || git clone -q --depth 1 --branch "$WEBAPPS_TAG" "$WEBAPPS_URL" "$SRC"
git -C "$SRC" cat-file -e "$WEBAPPS_COMMIT^{commit}" 2>/dev/null || { echo "web-apps: pinned commit $WEBAPPS_COMMIT not found"; exit 1; }
git -C "$SRC" checkout -q -f --detach "$WEBAPPS_COMMIT"
git -C "$SRC" clean -qfdx -e build/node_modules
if [ -f "$HERE/patches/web-apps/series" ]; then
    while read -r p; do
        case "$p" in ''|'#'*) continue;; esac
        git -C "$SRC" apply --whitespace=nowarn "$HERE/patches/web-apps/$p"
        echo "applied $p"
    done < "$HERE/patches/web-apps/series"
fi
cd "$SRC/build"
[ -d node_modules ] || nice -n 10 $NPM ci --ignore-scripts --omit=dev --no-audit --no-fund
G="nice -n 10 node node_modules/grunt/bin/grunt --no-color --force --sg-copy-images"
for t in deploy-api deploy-common-component deploy-documenteditor-component \
         deploy-spreadsheeteditor-component deploy-presentationeditor-component; do
    $G "$t" > "$OUT/webapps-$t.log" 2>&1 || { echo "web-apps: $t failed"; tail -20 "$OUT/webapps-$t.log"; exit 1; }
    echo "built $t"
done
for app in documenteditor spreadsheeteditor presentationeditor; do
    [ -f "$SRC/deploy/web-apps/apps/$app/main/app.js" ] || { echo "web-apps: no $app/main/app.js"; exit 1; }
done
rm -rf "$OUT/web-apps"
cp -a "$SRC/deploy/web-apps" "$OUT/web-apps"
echo "web-apps built: $OUT/web-apps ($(git -C "$SRC" describe --tags))"
