#!/bin/sh
# Build SG Office's native host -- ONLYOFFICE core (doctrenderer, docbuilder,
# x2t and their libraries) + our patches -- from source with gcc and qmake,
# against Debian's libraries: V8 from libnode, ICU, OpenSSL, zlib, Boost.
# Run it where the build dependencies are installed (build/inroot.sh runs it
# in the trixie build root build/mkroot.sh makes).
#
#   build/core.sh [WORK] [OUT]      (defaults /var/tmp/sgoffice/{src,out})
#
# Output: $OUT/host -- the documentbuilder layout upstream's deploy_builder.py
# makes (docbuilder, x2t, lib*.so, DoctRenderer.config, cmap.bin), without an
# sdkjs: build/assemble.sh adds ours. SG_CORE_JOBS: make -j (default 3).
# SG_CORE_ONLY: build only the projects whose name matches this regex.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
. "$HERE/upstream.conf"
WORK=${1:-/var/tmp/sgoffice/src}
OUT=${2:-/var/tmp/sgoffice/out}
JOBS=${SG_CORE_JOBS:-3}
CORE=$WORK/core
mkdir -p "$WORK" "$OUT"

pinned() {  # DIR URL TAG COMMIT -- a checkout of exactly the pinned commit
    [ -d "$1/.git" ] || git clone -q --depth 1 --branch "$3" "$2" "$1"
    git -C "$1" cat-file -e "$4^{commit}" 2>/dev/null || { echo "$1: pinned commit $4 not found"; exit 1; }
    [ "$(git -C "$1" rev-parse HEAD)" = "$4" ] || git -C "$1" checkout -q -f --detach "$4"
}
pinned "$CORE" "$CORE_URL" "$CORE_TAG" "$CORE_COMMIT"
pinned "$WORK/build_tools" "$BUILD_TOOLS_URL" "$BUILD_TOOLS_TAG" "$BUILD_TOOLS_COMMIT"
pinned "$WORK/sdkjs" "$SDKJS_URL" "$SDKJS_TAG" "$SDKJS_COMMIT"     # pdf engine's cmap.bin
pinned "$WORK/document-templates" "$TEMPLATES_URL" "$TEMPLATES_TAG" "$TEMPLATES_COMMIT"

# upstream's tracked tree, then our series (build output and the fetched
# third-party sources are git-ignored and kept, so rebuilds are incremental)
git -C "$CORE" checkout -q -f --detach "$CORE_COMMIT"
while read -r p; do
    case "$p" in ''|'#'*) continue;; esac
    git -C "$CORE" apply --whitespace=nowarn "$HERE/patches/core/$p"
    echo "applied $p"
done < "$HERE/patches/core/series"

# third-party sources upstream compiles in, at the commits upstream pins
# (its own fetch scripts); hyphen, which upstream leaves unpinned, at ours
T=$CORE/Common/3dParty
( cd "$T/html" && python3 fetch.py )
( cd "$T/harfbuzz" && python3 make.py )
( cd "$T/hunspell" && python3 before.py )
if [ ! -d "$T/hyphen/hyphen/.git" ]; then
    git clone -q "$HYPHEN_URL" "$T/hyphen/hyphen"
    git -C "$T/hyphen/hyphen" checkout -q "$HYPHEN_COMMIT"
fi
[ "$(git -C "$T/hyphen/hyphen" rev-parse HEAD)" = "$HYPHEN_COMMIT" ] || { echo "hyphen: not at $HYPHEN_COMMIT"; exit 1; }

# the projects upstream's "builder" module builds (build_tools sln.json), in order
PROJECTS="
Common/3dParty/cryptopp/project/cryptopp.pro
Common/cfcpp/cfcpp.pro
UnicodeConverter/UnicodeConverter.pro
Common/kernel.pro
Common/Network/network.pro
DesktopEditor/graphics/pro/graphics.pro
PdfFile/PdfFile.pro
DjVuFile/DjVuFile.pro
XpsFile/XpsFile.pro
HtmlFile2/HtmlFile2.pro
Fb2File/Fb2File.pro
EpubFile/CEpubFile.pro
HtmlRenderer/htmlrenderer.pro
DocxRenderer/DocxRenderer.pro
DesktopEditor/doctrenderer/doctrenderer.pro
OOXML/Projects/Linux/DocxFormatLib/DocxFormatLib.pro
OOXML/Projects/Linux/PPTXFormatLib/PPTXFormatLib.pro
OOXML/Projects/Linux/XlsbFormatLib/XlsbFormatLib.pro
MsBinaryFile/Projects/DocFormatLib/Linux/DocFormatLib.pro
MsBinaryFile/Projects/PPTFormatLib/Linux/PPTFormatLib.pro
MsBinaryFile/Projects/XlsFormatLib/Linux/XlsFormatLib.pro
MsBinaryFile/Projects/VbaFormatLib/Linux/VbaFormatLib.pro
TxtFile/Projects/Linux/TxtXmlFormatLib.pro
RtfFile/Projects/Linux/RtfFormatLib.pro
OdfFile/Projects/Linux/OdfFormatLib.pro
OOXML/Projects/Linux/BinDocument/BinDocument.pro
X2tConverter/build/Qt/X2tConverter.pro
DesktopEditor/AllFontsGen/AllFontsGen.pro
DesktopEditor/allthemesgen/allthemesgen.pro
DesktopEditor/doctrenderer/app_builder/docbuilder.pro
"
for pro in $PROJECTS; do
    n=$(basename "$pro" .pro)
    [ -z "${SG_CORE_ONLY:-}" ] || echo "$n" | grep -Eq "$SG_CORE_ONLY" || continue
    d=$CORE/$(dirname "$pro")
    ( cd "$d" && rm -f .qmake.stash Makefile.sg &&
      qmake -nocache "$CORE/$pro" "CONFIG+=builder release disable_precompiled_header sg_debian" -o Makefile.sg &&
      nice -n 10 make -f Makefile.sg -j"$JOBS" ) > "$OUT/core-$n.log" 2>&1 ||
        { echo "core: $n FAILED (log: $OUT/core-$n.log)"; tail -20 "$OUT/core-$n.log"; exit 1; }
    echo "built $n"
done
[ -n "${SG_CORE_ONLY:-}" ] && exit 0

# deploy the host the way upstream's deploy_builder.py lays it out
H=$OUT/host
rm -rf "$H"; mkdir -p "$H"
L=$CORE/build/lib/linux_64; B=$CORE/build/bin/linux_64
for l in kernel UnicodeConverter kernel_network graphics PdfFile DjVuFile XpsFile HtmlFile2 HtmlRenderer \
         Fb2File EpubFile DocxRenderer doctrenderer; do
    cp -L "$L/lib$l.so" "$H/"
done
cp "$B/x2t" "$B/docbuilder" "$H/"
cp "$WORK/sdkjs/pdf/src/engine/cmap.bin" "$H/cmap.bin"
cp "$B/allfontsgen" "$B/allthemesgen" "$H/" 2>/dev/null || true
mkdir -p "$H/empty" "$H/dictionaries"
cp "$WORK/document-templates/new/en-US/"new.* "$H/empty/"
cat > "$H/DoctRenderer.config" <<'CFG'
<Settings>
<file>./sdkjs/common/Native/native.js</file>
<file>./sdkjs/common/Native/jquery_native.js</file>
<allfonts>./sdkjs/common/AllFonts.js</allfonts>
<file>./sdkjs/vendor/xregexp/xregexp-all-min.js</file>
<sdkjs>./sdkjs</sdkjs>
<dictionaries>./dictionaries</dictionaries>
</Settings>
CFG
echo "host built: $H (core $(git -C "$CORE" describe --tags) + $(grep -cv '^#' "$HERE/patches/core/series" || true) patch(es))"
