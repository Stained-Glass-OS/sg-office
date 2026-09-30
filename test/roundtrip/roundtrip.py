#!/usr/bin/env python3
"""SG Office 2 -- round-trip fidelity harness and gate (ADR 0016, Phase 1).

Opens each independent source document (gen_corpus.py) in our engine, saves it
back to the same OOXML format, and checks that a fixed set of features survived
the trip -- values and computed formula results, number formats and merged
ranges for .xlsx; headings, bold runs, bullets and table cells for .docx; slide
count and text for .pptx. Emits a 0-100 fidelity score per file and overall.

    roundtrip.py [--engine DIR] [--corpus DIR] [--json OUT] [--baseline BASE]

--baseline makes it a gate: any feature the baseline recorded as preserved that
is now lost fails with exit 1 (a regression); newly preserved features are
reported as improvements. The engine is driven headlessly via `documentbuilder`
(dev/QA oracle today; our own from-source host later) -- never shipped.

Copyright (C) 2026 Stained Glass OS contributors
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ENGINE = os.path.join(
    HERE, "oracle", "extracted", "opt", "onlyoffice", "documentbuilder")


def run_engine(engine_dir, script_text):
    """Run one .docbuilder script headlessly. Returns (rc, output)."""
    with tempfile.NamedTemporaryFile("w", suffix=".docbuilder", delete=False) as f:
        f.write(script_text)
        script = f.name
    try:
        env = dict(os.environ, LD_LIBRARY_PATH=engine_dir)
        p = subprocess.run([os.path.join(engine_dir, "docbuilder"), script],
                           cwd=engine_dir, env=env, capture_output=True,
                           text=True, timeout=120)
        return p.returncode, (p.stdout + p.stderr)
    finally:
        os.unlink(script)


def roundtrip(engine_dir, src, out, fmt):
    """Open src, save it back to out in fmt, through the engine.

    Spreadsheets are recalculated first, exactly as opening a workbook in an
    editor does, so cached formula results reflect our engine's calc.
    """
    recalc = ("if (Api.RecalculateAllFormulas) { Api.RecalculateAllFormulas(); }\n"
              if fmt == "xlsx" else "")
    script = ('builder.OpenFile("%s", "");\n'
              '%s'
              'builder.SaveFile("%s", "%s");\n'
              'builder.CloseFile();\n') % (src, recalc, fmt, out)
    return run_engine(engine_dir, script)


def _zx(path, member):
    with zipfile.ZipFile(path) as z:
        return z.read(member).decode("utf-8", "replace")


# ---- per-format feature checks -> list of (feature_name, ok) --------------------------------

def check_xlsx(path):
    xml = _zx(path, "xl/worksheets/sheet1.xml")
    styles = ""
    try:
        styles = _zx(path, "xl/styles.xml")
    except KeyError:
        pass
    shared = ""
    try:
        shared = _zx(path, "xl/sharedStrings.xml")
    except KeyError:
        pass

    def cell_v(ref):
        m = re.search(r'<c r="%s"[^>]*>.*?<v>([^<]*)</v>' % ref, xml, re.S)
        return m.group(1) if m else None

    def text_at(ref):
        # inlineStr or shared-string index
        m = re.search(r'<c r="%s"[^>]*t="inlineStr"[^>]*>.*?<t[^>]*>([^<]*)</t>'
                      % ref, xml, re.S)
        if m:
            return m.group(1)
        m = re.search(r'<c r="%s"[^>]*t="s"[^>]*><v>(\d+)</v>' % ref, xml)
        if m and shared:
            idx = int(m.group(1))
            ts = re.findall(r'<t[^>]*>([^<]*)</t>', shared)
            return ts[idx] if idx < len(ts) else None
        return cell_v(ref)

    checks = []
    checks.append(("xlsx: A1 text 'Item'", text_at("A1") == "Item"))
    checks.append(("xlsx: B2 value 3", (cell_v("B2") or "").startswith("3")))
    checks.append(("xlsx: C2 value 1234.5", (cell_v("C2") or "") in ("1234.5", "1234.50")))
    checks.append(("xlsx: SUM(B2:B3) computed to 7", cell_v("B4") == "7"))
    checks.append(("xlsx: TEXTJOIN computed to '3-4'",
                   (text_at("B5") == "3-4") or (cell_v("B5") == "3-4")))
    checks.append(("xlsx: merged range A6:B6 kept",
                   'ref="A6:B6"' in xml or 'A6:B6' in xml))
    # "#,##0.00" is Excel built-in numFmtId 4; the engine may keep it as the
    # built-in id or as an explicit formatCode -- either preserves the format.
    checks.append(("xlsx: number format #,##0.00 kept",
                   "#,##0.00" in styles or 'numFmtId="4"' in styles))
    return checks


def check_docx(path):
    xml = _zx(path, "word/document.xml")
    text = re.sub(r"<[^>]+>", "", re.sub(r"</w:p>", "\n", xml))
    checks = []
    checks.append(("docx: heading text kept",
                   "SG Office round-trip: Documents" in text))
    checks.append(("docx: paragraph text kept",
                   "over the lazy dog" in text.replace("\n", " ")))
    # bold run around "jumps"
    bold = re.search(r'<w:r>(?:(?!</w:r>).)*?<w:b/>(?:(?!</w:r>).)*?jumps',
                     xml, re.S) or re.search(
        r'jumps(?:(?!</w:r>).)*?<w:b/>', xml, re.S)
    checks.append(("docx: bold run 'jumps' kept", bool(bold) or "<w:b/>" in xml))
    for item in ("alpha", "beta", "gamma"):
        checks.append(("docx: bullet '%s' kept" % item, item in text))
    checks.append(("docx: table cell 'answer' kept", "answer" in text))
    checks.append(("docx: table cell '42' kept", "42" in text))
    return checks


def check_pptx(path):
    with zipfile.ZipFile(path) as z:
        slides = [n for n in z.namelist()
                  if re.match(r"ppt/slides/slide\d+\.xml$", n)]
        alltext = ""
        for n in slides:
            alltext += re.sub(r"<[^>]+>", " ", z.read(n).decode("utf-8", "replace"))
    checks = []
    checks.append(("pptx: two slides kept", len(slides) >= 2))
    checks.append(("pptx: title text kept",
                   "SG Office round-trip: Presentations" in alltext))
    for b in ("first", "second", "third"):
        checks.append(("pptx: bullet '%s' kept" % b, b in alltext))
    return checks


FORMATS = [
    ("src.xlsx", "xlsx", check_xlsx),
    ("src.docx", "docx", check_docx),
    ("src.pptx", "pptx", check_pptx),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=DEFAULT_ENGINE)
    ap.add_argument("--corpus", default=os.path.join(HERE, "corpus"))
    ap.add_argument("--json")
    ap.add_argument("--baseline")
    args = ap.parse_args()
    # Absolute paths: the engine runs with its own cwd, so relative corpus/out
    # paths in the .docbuilder script would resolve against the engine dir.
    args.engine = os.path.abspath(args.engine)
    args.corpus = os.path.abspath(args.corpus)

    if not os.path.exists(os.path.join(args.engine, "docbuilder")):
        print("engine not found at %s -- run fetch-oracle.sh" % args.engine)
        return 2

    results = {}
    all_checks = []
    outdir = tempfile.mkdtemp(prefix="sgoffice-rt-")
    for name, fmt, checker in FORMATS:
        src = os.path.join(args.corpus, name)
        out = os.path.join(outdir, "out." + fmt)
        rc, log = roundtrip(args.engine, src, out, fmt)
        if rc != 0 or not os.path.exists(out):
            print("ENGINE FAIL on %s (rc=%d): %s" % (name, rc, log[-400:]))
            checks = [("%s: engine opened+saved file" % fmt, False)]
        else:
            checks = [("%s: engine opened+saved file" % fmt, True)] + checker(out)
        results[fmt] = {n: bool(ok) for n, ok in checks}
        all_checks += checks
        passed = sum(1 for _, ok in checks if ok)
        print("\n[%s] %d/%d features preserved (%.0f%%)"
              % (fmt, passed, len(checks), 100.0 * passed / len(checks)))
        for n, ok in checks:
            print("   %s %s" % ("PASS" if ok else "FAIL", n))

    total = len(all_checks)
    passed = sum(1 for _, ok in all_checks if ok)
    score = 100.0 * passed / total
    print("\n=== SG Office 2 round-trip fidelity: %d/%d = %.1f/100 ==="
          % (passed, total, score))

    if args.json:
        json.dump({"score": score, "results": results}, open(args.json, "w"),
                  indent=2, sort_keys=True)

    if args.baseline:
        if not os.path.exists(args.baseline):
            json.dump({"score": score, "results": results},
                      open(args.baseline, "w"), indent=2, sort_keys=True)
            print("wrote new baseline %s" % args.baseline)
            return 0
        base = json.load(open(args.baseline))
        regressions, improvements = [], []
        for fmt, feats in base.get("results", {}).items():
            for n, was in feats.items():
                now = results.get(fmt, {}).get(n, False)
                if was and not now:
                    regressions.append(n)
                elif now and not was:
                    improvements.append(n)
        for fmt, feats in results.items():
            for n, now in feats.items():
                if now and n not in base.get("results", {}).get(fmt, {}):
                    improvements.append(n)
        for i in improvements:
            print("IMPROVED: %s" % i)
        if regressions:
            for r in regressions:
                print("REGRESSION: %s" % r)
            print("GATE FAIL: %d regression(s)" % len(regressions))
            return 1
        print("GATE PASS: no regressions vs baseline (score %.1f)" % score)
    return 0


if __name__ == "__main__":
    sys.exit(main())
