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
#   make test           lint + round-trip gate + Excel corpus gate on $(OUT)/engine
#   make test-mutation  prove the corpus gate fails on unpatched upstream
#
# HOST: the native host to assemble with. Ours ($(OUT)/host) once `make core`
# has run; SG_DEV_HOST may name a dev/QA host meanwhile (never shipped).
WORK ?= /var/tmp/sgoffice/src
OUT  ?= /var/tmp/sgoffice/out
HOST ?= $(if $(wildcard $(OUT)/host/docbuilder),$(OUT)/host,$(SG_DEV_HOST))
PY   ?= python3
# the builds run in the trixie build root (build/mkroot.sh); INROOT= to run on the host
INROOT ?= $(if $(wildcard /var/tmp/sgoffice/root-build/usr/bin/qmake),SG_CWD=$(CURDIR) sh build/inroot.sh,)

.PHONY: all lint sdkjs core engine test test-roundtrip test-parity test-mutation
all: sdkjs core engine

lint:
	@for f in build/*.sh; do sh -n $$f || exit 1; done
	@$(PY) -m py_compile test/parity/engine_corpus.py test/roundtrip/roundtrip.py test/roundtrip/ooxmlw.py test/roundtrip/mutate.py
	@for d in patches/*/; do $(PY) tools/trademark-check.py --allow tools/trademark-allow.txt --patches $$d build test/parity/engine_corpus.py || exit 1; done
	@for d in patches/*/; do while read -r p; do case "$$p" in ''|'#'*) ;; *) [ -f "$$d$$p" ] || { echo "series names missing $$d$$p"; exit 1; };; esac; done < $$d/series; done
	@echo "lint: OK"

sdkjs:
	@$(INROOT) sh build/sdkjs.sh $(WORK) $(OUT)

core:
	@$(INROOT) sh build/core.sh $(WORK) $(OUT)

engine:
	@[ -n "$(HOST)" ] || { echo "no native host: run make core (or set SG_DEV_HOST)"; exit 1; }
	@sh build/assemble.sh $(HOST) $(OUT)/sdkjs $(WORK)/web-apps $(OUT)/engine

test: lint test-roundtrip test-parity

# the round-trip corpus is written by our own code (ooxmlw.py), not stored
$(OUT)/rt-corpus/src.docx: test/roundtrip/ooxmlw.py
	@$(PY) test/roundtrip/ooxmlw.py $(OUT)/rt-corpus >/dev/null

test-roundtrip: $(OUT)/rt-corpus/src.docx
	@$(PY) test/roundtrip/roundtrip.py --engine $(OUT)/engine --corpus $(OUT)/rt-corpus --baseline test/roundtrip/baseline.json

test-parity:
	@$(PY) test/parity/engine_corpus.py --engine $(OUT)/engine --baseline test/parity/baseline.json --json $(OUT)/parity.json

# Both gates must be able to fail. The corpus gate on upstream without our
# patches (they make the baseline's PERCENTOF cases match): unpatched sdkjs
# into $(OUT)/mutant, assembled, must fail test-parity. The round-trip gate on
# a corpus whose .xlsx lost its merged range and SUM formula must fail too.
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
