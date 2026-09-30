#!/bin/sh
# Assemble a runnable engine: a native host (doctrenderer/docbuilder/x2t) plus
# OUR sdkjs build. No V8 snapshot is carried over, so the host loads our
# sdk-all-min.js and caches its own code.
#
#   build/assemble.sh HOSTDIR SDKJSDIR WEBAPPS_SRC OUTDIR
#
# HOSTDIR  a documentbuilder tree (M2a: the dev/QA oracle's host; M2b: ours)
# SDKJSDIR build/sdkjs.sh's output;  WEBAPPS_SRC the web-apps checkout, whose
# vendor/ supplies xregexp and jQuery exactly as upstream's builder ships them
set -eu
HOST=$1; SDKJS=$2; SRC=$3; OUT=$4
[ -x "$HOST/docbuilder" ] || { echo "no docbuilder in $HOST"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT"
( cd "$HOST" && tar cf - --exclude=./sdkjs . ) | ( cd "$OUT" && tar xf - )
cp -a "$SDKJS" "$OUT/sdkjs"
mkdir -p "$OUT/sdkjs/vendor/jquery" "$OUT/sdkjs/vendor/xregexp"
cp "$SRC/vendor/jquery/jquery.min.js" "$SRC/vendor/jquery.browser/dist/jquery.browser.min.js" "$OUT/sdkjs/vendor/jquery/"
cp "$SRC/vendor/xregexp/xregexp-all-min.js" "$OUT/sdkjs/vendor/xregexp/"
# the font tables are generated per machine from its fonts (AllFontsGen); take
# the host's until our AllFontsGen runs (M2b)
for f in AllFonts.js font_selection.bin; do
    [ -f "$OUT/sdkjs/common/$f" ] || cp "$HOST/sdkjs/common/$f" "$OUT/sdkjs/common/$f"
done
find "$OUT/sdkjs" \( -name 'sdk-all.bin' -o -name 'sdk-all.cache' \) -delete
echo "engine assembled: $OUT"
