# SG Office

Stained Glass OS's own office suite -- Documents, Spreadsheets and
Presentations -- **based on ONLYOFFICE** (see NOTICE), built from source with an
open toolchain, so its engine is ours to fix and extend
([ADR 0016](https://github.com/Stained-Glass-OS/stained-glass/blob/main/docs/decisions/0016-our-own-office-suite.md)).

It ships as the Debian package `sg-office-editors` (`make deb`); sg-shell's
`sg-office` package depends on it and puts SG Office Documents, Spreadsheets
and Presentations in Start and on Office's file types.

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
| `app/` | the program: a window per document (Qt 6, QtWebEngine from Debian), SG-drawn title bar, `bridge.js` answering the editors' calls, x2t for open/save |
| `build/webapps.sh` | the editors' interface (web-apps) with node and grunt; no downloaded binaries (`patches/web-apps`) |
| `build/assemble-app.sh` | lays the program out as it installs (`bin/`, `lib/sg-office/engine`, `share/sg-office`) |
| `debian/` | the package `sg-office-editors`: `debian/rules` builds everything from source and packages `app-root`; `debian/copyright` credits ONLYOFFICE |
| `test/deb/` | the package's gate: contents, licence notices, dependencies, then installed (dpkg) into a scratch root and the program's gate run as installed |
| `test/app/` | the program's gate: headless (own Xvfb, scratch HOME), clicks, types, saves, Saves As OpenDocument, checks the SG look |
| `test/roundtrip/` | a .docx/.xlsx/.pptx our own code writes from scratch (`ooxmlw.py`), opened in the engine and saved back; their features must survive |
| `test/parity/` | the Excel formula corpus (775 cases, 530 functions; vendored from sg-shell `office/parity`) run through our engine |
| `tools/trademark-check.py` | the project's trademark gate, over our patches and scripts |

## Building and testing

    build/mkroot.sh                 # once: the build root
    make sdkjs core engine          # the engine, assembled in $(OUT)/engine
    make webapps sdkjs-desktop app  # the editors and the program, in $(OUT)/app-root
    make test                       # lint + round-trip, Excel corpus and program gates
    make test-mutation              # every gate must fail when what it guards breaks
    make deb                        # ../sg-office-editors_VERSION_amd64.deb, from source (DEB_OUT trees)
    make test-deb                   # the package's gate; make test-deb-mutation: its broken packages

Builds keep their trees under `/var/tmp/sgoffice` and run niced with `-j3`.

## Changing the engine

Make the change in the upstream checkout (`/var/tmp/sgoffice/src/sdkjs` or
`.../core`), export it as the next numbered patch in `patches/<repo>/`, add it
to that `series`, rebuild, and re-record `test/parity/baseline.json` when
cases newly match. A patch that makes a corpus case match Excel is the unit of
progress; `make test` refuses anything that makes a matching case stop
matching.
