# SG Office -- our office suite's engine, built from source (ADR 0016).
#
# The engine is ONLYOFFICE's (sdkjs: the document/calc engines in JavaScript;
# core: the native host, converter and renderer in C++), pinned in
# upstream.conf, with our patches in patches/. Everything builds with an open
# toolchain (gcc, node, Google Closure Compiler) against Debian's libraries.
#
#   make sdkjs          build the JS engine (+ patches)       -> $(OUT)/sdkjs
#   make core           build the native host (+ patches)     -> $(OUT)/host
#   make engine         assemble a runnable engine            -> $(OUT)/engine
#   make webapps        build the editors' interface          -> $(OUT)/web-apps
#   make sdkjs-desktop  the JS engine for the editors         -> $(OUT)/sdkjs-desktop
#   make app            build the program, lay it out         -> $(OUT)/app-root
#   make test           lint + round-trip, Excel corpus and program gates
#   make test-mutation  prove each gate fails when what it guards breaks
#   make deb            the package sg-office-editors, built from source by
#                       debian/rules (its own build trees: DEB_OUT)  -> ../sg-office-editors_*.deb
#   make test-deb       the package's gate: contents, dependencies, and the
#                       program's gate run with the package installed
#   make test-deb-mutation  broken packages test-deb must refuse
#
# HOST: the native host to assemble with. Ours ($(OUT)/host) once `make core`
# has run; SG_DEV_HOST may name a dev/QA host meanwhile (never shipped).
WORK ?= /var/tmp/sgoffice/src
OUT  ?= /var/tmp/sgoffice/out
HOST ?= $(if $(wildcard $(OUT)/host/docbuilder),$(OUT)/host,$(SG_DEV_HOST))
PY   ?= python3
# no __pycache__ in the tree: the package gate runs the tests in a user
# namespace, whose files dh_clean (as the user) then could not remove, and the
# next release's package build stopped
export PYTHONDONTWRITEBYTECODE := 1
# the builds run in the trixie build root (build/mkroot.sh); INROOT= to run on the host
INROOT ?= $(if $(wildcard /var/tmp/sgoffice/root-build/usr/bin/qmake),SG_CWD=$(CURDIR) sh build/inroot.sh,)

.PHONY: all lint sdkjs core engine webapps sdkjs-desktop app test test-roundtrip test-parity test-app test-mutation test-app-mutation deb test-deb test-deb-mutation
all: sdkjs core engine webapps sdkjs-desktop app

lint:
	@for f in build/*.sh; do sh -n $$f || exit 1; done
	@$(PY) -m py_compile test/parity/engine_corpus.py test/roundtrip/roundtrip.py test/roundtrip/ooxmlw.py test/roundtrip/mutate.py test/app/app_check.py test/app/features_check.py test/app/mutate_bridge.py test/deb/deb_check.py test/deb/mutate_deb.py
	@node --check app/res/bridge.js
	@for d in patches/*/; do $(PY) tools/trademark-check.py --allow tools/trademark-allow.txt --patches $$d build app test/parity/engine_corpus.py || exit 1; done
	@for d in patches/*/; do while read -r p; do case "$$p" in ''|'#'*) ;; *) [ -f "$$d$$p" ] || { echo "series names missing $$d$$p"; exit 1; };; esac; done < $$d/series; done
	@echo "lint: OK"

sdkjs:
	@$(INROOT) sh build/sdkjs.sh $(WORK) $(OUT)

core:
	@$(INROOT) sh build/core.sh $(WORK) $(OUT)

engine:
	@[ -n "$(HOST)" ] || { echo "no native host: run make core (or set SG_DEV_HOST)"; exit 1; }
	@sh build/assemble.sh $(HOST) $(OUT)/sdkjs $(WORK)/web-apps $(OUT)/engine

webapps:
	@$(INROOT) sh build/webapps.sh $(WORK) $(OUT)

sdkjs-desktop:
	@SG_SDKJS_DESKTOP=1 $(INROOT) sh build/sdkjs.sh $(WORK) $(OUT)

# the program (Qt 6 / QtWebEngine), laid out as it installs
app:
	@$(INROOT) sh -c 'cmake -S app -B $(OUT)/app-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null && nice -n 10 ninja -C $(OUT)/app-build -j3 >/dev/null'
	@sh build/assemble-app.sh $(OUT)/app-build/sg-office $(OUT)/engine $(OUT)/web-apps $(OUT)/sdkjs-desktop $(OUT)/app-root

test: lint test-roundtrip test-parity test-app

# the round-trip corpus is written by our own code (ooxmlw.py), not stored
$(OUT)/rt-corpus/src.docx: test/roundtrip/ooxmlw.py
	@$(PY) test/roundtrip/ooxmlw.py $(OUT)/rt-corpus >/dev/null

test-roundtrip: $(OUT)/rt-corpus/src.docx
	@$(PY) test/roundtrip/roundtrip.py --engine $(OUT)/engine --corpus $(OUT)/rt-corpus --baseline test/roundtrip/baseline.json

test-app:
	@$(PY) test/app/app_check.py --root $(OUT)/app-root --work $(OUT)/app-check
	@$(PY) test/app/features_check.py --root $(OUT)/app-root --work $(OUT)/features-check

test-parity:
	@$(PY) test/parity/engine_corpus.py --engine $(OUT)/engine --baseline test/parity/baseline.json --json $(OUT)/parity.json

# Both gates must be able to fail. The corpus gate on upstream without our
# patches (they make the baseline's PERCENTOF cases match): unpatched sdkjs
# into $(OUT)/mutant, assembled, must fail test-parity. The round-trip gate on
# a corpus whose .xlsx lost its merged range and SUM formula must fail too.
# The program gate with a bridge that drops the editor's changes, fakes a save,
# or leaves out SG Office's theme must fail too.
test-mutation:
	@SG_SDKJS_NOPATCH=1 $(INROOT) sh build/sdkjs.sh $(WORK) $(OUT)/mutant
	@sh build/assemble.sh $(HOST) $(OUT)/mutant/sdkjs $(WORK)/web-apps $(OUT)/mutant/engine
	@if $(PY) test/parity/engine_corpus.py --engine $(OUT)/mutant/engine --baseline test/parity/baseline.json; then \
	    echo "test-mutation: FAIL -- the gate passed without our patches"; exit 1; \
	else echo "test-mutation: OK -- the corpus gate catches unpatched upstream"; fi
	@$(PY) test/roundtrip/ooxmlw.py $(OUT)/rt-corpus >/dev/null
	@$(PY) test/roundtrip/mutate.py $(OUT)/rt-corpus $(OUT)/mutant/corpus
	@if $(PY) test/roundtrip/roundtrip.py --engine $(OUT)/engine --corpus $(OUT)/mutant/corpus --baseline test/roundtrip/baseline.json; then \
	    echo "test-mutation: FAIL -- the round-trip gate passed a damaged document"; exit 1; \
	else echo "test-mutation: OK -- the round-trip gate catches lost features"; fi
	@for m in nochanges fakesave notheme; do \
	    $(PY) test/app/mutate_bridge.py $$m $(OUT)/mutant/bridge-$$m.js >/dev/null || exit 1; \
	    if $(PY) test/app/app_check.py --root $(OUT)/app-root --work $(OUT)/mutant/app-$$m \
	         --bridge $(OUT)/mutant/bridge-$$m.js --only docx --no-odf >/dev/null; then \
	        echo "test-mutation: FAIL -- the program gate passed bridge mutant $$m"; exit 1; \
	    else echo "test-mutation: OK -- the program gate catches bridge mutant $$m"; fi; done
	@$(MAKE) -s test-app-mutation

# The features gate against its mutants: bridge.js ones (NAME:CHECK), and the
# program built with one fix reverted (#ifdef SG_MUTANT_NAME; built into
# $(OUT)/mutant/app-NAME, run with the app-root's engine and editors).
BRIDGE_MUTANTS = noprint:print notitle:title
APP_MUTANTS = FONTS_EVERY_START:fonts CSV_NO_PARAMS:csv EXPORT_SAVES:export NO_ALTF4:altf4 UNITS_CM:units \
              NO_HANDOFF:handoff NO_DARK:dark NO_RECENTS:recents \
              NO_TASKBAR_ICON:taskbar FAINT_CLOSE:closeglyph
test-app-mutation:
	@for mc in $(BRIDGE_MUTANTS); do m=$${mc%%:*}; c=$${mc##*:}; \
	    $(PY) test/app/mutate_bridge.py $$m $(OUT)/mutant/bridge-$$m.js >/dev/null || exit 1; \
	    if $(PY) test/app/features_check.py --root $(OUT)/app-root --work $(OUT)/mutant/feat-$$m \
	         --bridge $(OUT)/mutant/bridge-$$m.js --only $$c >/dev/null; then \
	        echo "test-mutation: FAIL -- the features gate passed bridge mutant $$m"; exit 1; \
	    else echo "test-mutation: OK -- the features gate ($$c) catches bridge mutant $$m"; fi; done
	@for mc in $(APP_MUTANTS); do m=$${mc%%:*}; c=$${mc##*:}; r=$(OUT)/mutant/app-$$m; \
	    $(INROOT) sh -c "cmake -S app -B $$r-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSG_MUTANTS=$$m >/dev/null && nice -n 10 ninja -C $$r-build -j3 >/dev/null" || exit 1; \
	    rm -rf $$r; mkdir -p $$r/bin; cp $$r-build/sg-office $$r/bin/; \
	    ln -s $(OUT)/app-root/lib $$r/lib; ln -s $(OUT)/app-root/share $$r/share; \
	    if $(PY) test/app/features_check.py --root $$r --work $(OUT)/mutant/feat-$$m --only $$c >/dev/null; then \
	        echo "test-mutation: FAIL -- the features gate passed program mutant $$m"; exit 1; \
	    else echo "test-mutation: OK -- the features gate ($$c) catches program mutant $$m"; fi; done

# --- the package -------------------------------------------------------------
# debian/rules builds everything from source (make sdkjs core engine webapps
# sdkjs-desktop app) into DEB_OUT -- its own trees, apart from development's
# OUT -- sharing WORK's upstream checkouts and object files, so a rebuild is
# incremental. The newest ../sg-office-editors_*_amd64.deb is the one tested.
DEB_OUT ?= /var/tmp/sgoffice/out-deb
DEB = $(shell ls ../sg-office-editors_*_amd64.deb 2>/dev/null | sort -V | tail -1)
deb:
	OUT=$(DEB_OUT) WORK=$(WORK) nice -n 10 dpkg-buildpackage -us -uc -b -d

test-deb:
	@[ -n "$(DEB)" ] || { echo "no ../sg-office-editors_*_amd64.deb: make deb"; exit 1; }
	@$(PY) test/deb/deb_check.py --deb $(DEB) --work $(DEB_OUT)/deb-check

test-deb-mutation:
	@[ -n "$(DEB)" ] || { echo "no ../sg-office-editors_*_amd64.deb: make deb"; exit 1; }
	@mkdir -p $(DEB_OUT)/mutant
	@for m in nocredit nodep moved x2tnoexec tips; do \
	    $(PY) test/deb/mutate_deb.py $$m $(DEB) $(DEB_OUT)/mutant/$$m.deb || exit 1; \
	    if $(PY) test/deb/deb_check.py --deb $(DEB_OUT)/mutant/$$m.deb --work $(DEB_OUT)/mutant/check-$$m >$(DEB_OUT)/mutant/check-$$m.log 2>&1; then \
	        echo "test-deb-mutation: FAIL -- the package gate passed mutant $$m"; exit 1; \
	    else echo "test-deb-mutation: OK -- the package gate catches $$m ($$(grep -c '   FAIL' $(DEB_OUT)/mutant/check-$$m.log) failed checks)"; fi; done
