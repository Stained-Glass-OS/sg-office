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
cp -a "$WEBAPPS" "$ROOT/share/sg-office/web-apps"
cp -a "$SDKJS" "$ROOT/share/sg-office/sdkjs"
find "$ROOT/share/sg-office/sdkjs" \( -name 'sdk-all.bin' -o -name 'sdk-all.cache' \) -delete
echo "app assembled: $ROOT"
