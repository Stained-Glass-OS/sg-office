# SG Office

Stained Glass OS's own office suite -- Documents, Spreadsheets and
Presentations -- **based on ONLYOFFICE** (see NOTICE), built from source with an
open toolchain, so its engine is ours to fix and extend
([ADR 0016](https://github.com/Stained-Glass-OS/stained-glass/blob/main/docs/decisions/0016-our-own-office-suite.md)).

Until SG Office passes its gates, the shipping suite stays the interim
LibreOffice-based `sg-office` (sg-shell `office/`, ADR 0015).

## What is here

| | |
|---|---|
| `upstream.conf` | the ONLYOFFICE sources we build, pinned by tag **and** commit |
| `patches/sdkjs/` | our changes to the JavaScript engines (a `series` of patches) |
| `patches/core/` | our changes to the native core: Debian build (`sg_debian`), GCC 14 |
| `build/sdkjs.sh` | JS engine: node + Google Closure Compiler (native npm binary, no Java) |
| `build/core.sh` | native host: gcc + qmake against Debian's V8 (libnode), ICU, OpenSSL, zlib, Boost |
| `build/assemble.sh` | a runnable engine = host + our sdkjs (no upstream snapshot carried over) |
| `build/mkroot.sh`, `build/inroot.sh` | the rootless trixie build root the builds run in |
| `test/roundtrip/` | .docx/.xlsx/.pptx made by other tools, opened and saved back; features must survive |
| `test/parity/` | the Excel formula corpus (775 cases, 530 functions; vendored from sg-shell `office/parity`) run through our engine |
| `tools/trademark-check.py` | the project's trademark gate, over our patches and scripts |

## Building and testing

    build/mkroot.sh                 # once: the build root
    make sdkjs core engine          # build everything, assemble $(OUT)/engine
    make test                       # lint + round-trip gate + Excel corpus gate
    make test-mutation              # the corpus gate must fail without our patches

Builds keep their trees under `/var/tmp/sgoffice` and run niced with `-j3`.

## Changing the engine

Make the change in the upstream checkout (`/var/tmp/sgoffice/src/sdkjs` or
`.../core`), export it as the next numbered patch in `patches/<repo>/`, add it
to that `series`, rebuild, and re-record `test/parity/baseline.json` when
cases newly match. A patch that makes a corpus case match Excel is the unit of
progress; `make test` refuses anything that makes a matching case stop
matching.
