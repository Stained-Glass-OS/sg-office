#!/bin/sh
# Lay out SG Office as it installs, under ROOT (a staging /usr):
#   ROOT/bin/sg-office                  the program (app/, built with cmake)
#   ROOT/lib/sg-office/engine/          the native engine (x2t, allfontsgen, lib*.so,
#                                       new-document templates, its sdkjs)
#   ROOT/share/sg-office/web-apps/      the editors' interface
#   ROOT/share/sg-office/sdkjs/         the editors' engine, desktop variant
#
#   build/assemble-app.sh APPBIN ENGINE WEBAPPS SDKJS_DESKTOP ROOT
set -eu
APPBIN=$1; ENGINE=$2; WEBAPPS=$3; SDKJS=$4; ROOT=$5
for x in "$APPBIN" "$ENGINE/x2t" "$ENGINE/allfontsgen" "$WEBAPPS/apps/api/documents/api.js" "$SDKJS/word/sdk-all.js"; do
    [ -e "$x" ] || { echo "assemble-app: missing $x"; exit 1; }
done
rm -rf "$ROOT"
mkdir -p "$ROOT/bin" "$ROOT/lib/sg-office" "$ROOT/share/sg-office"
cp "$APPBIN" "$ROOT/bin/sg-office"
cp -a "$ENGINE" "$ROOT/lib/sg-office/engine"
rm -f "$ROOT/lib/sg-office/engine/docbuilder"        # the program uses x2t only
# one sdkjs: x2t (saving: the editor's changes replayed) runs the editors'
# own, as upstream's desktop app does; its font tables are the user's cache
E=$ROOT/lib/sg-office/engine
mkdir -p "$ROOT/share/sg-office/sdkjs-vendor"
cp -a "$E/sdkjs/vendor/." "$ROOT/share/sg-office/sdkjs-vendor/"
rm -rf "$E/sdkjs"
ln -s ../../../share/sg-office/sdkjs "$E/sdkjs"
cp -a "$WEBAPPS" "$ROOT/share/sg-office/web-apps"
# what the desktop editors do not use (as upstream's desktop packaging): the
# mobile and embedded variants, source maps, and the help pages -- 0.46 GB of
# screenshots in many languages; SG Office turns in-editor help off
W=$ROOT/share/sg-office/web-apps/apps
rm -rf "$W"/*/mobile "$W"/*/embed "$W"/*/main/resources/help
find "$ROOT/share/sg-office/web-apps" -name '*.map' -delete
cp -a "$SDKJS" "$ROOT/share/sg-office/sdkjs"
mv "$ROOT/share/sg-office/sdkjs-vendor" "$ROOT/share/sg-office/sdkjs/vendor"
find "$ROOT/share/sg-office/sdkjs" \( -name 'sdk-all.bin' -o -name 'sdk-all.cache' \) -delete
# the editors' "new feature" tips (Updated Pivot Tables, ...): each shows
# until its name is in the editors' storage; SG Office marks them all seen
# (editor.html) -- the names as this build of the editors has them
TIPS=$(grep -rhoE "[\"']([a-z]+-)?help-tip-[a-z0-9-]+[\"']" "$ROOT/share/sg-office/web-apps/apps" | tr -d "\"'" | sort -u)
[ -n "$TIPS" ] || { echo "assemble-app: no help tips found in the editors (has upstream renamed them?)"; exit 1; }
{ printf 'window.sgSeenTips = ['; printf '"%s",' $TIPS | sed 's/,$//'; printf '];\n'; } \
    > "$ROOT/share/sg-office/web-apps/sg-seen-tips.js"
echo "app assembled: $ROOT"
