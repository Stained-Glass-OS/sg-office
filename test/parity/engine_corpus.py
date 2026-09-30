#!/usr/bin/python3
"""SG Office -- run the Excel formula corpus through OUR engine.

    engine_corpus.py --engine DIR [--json RES.json] [--report OUT.md]
                     [--baseline BASE.json] [--only REGEX] [--keep DIR]

The corpus, the workbook it builds (formulas exactly as Excel writes them)
and the verdicts are sg-shell's office/parity/corpus.py, vendored here (see
SYNCED-FROM). Where corpus.py drives LibreOffice through UNO, this drives our
engine headlessly: documentbuilder opens the workbook, recalculates it
(Api.RecalculateAllFormulas, as opening it in the editor does), saves it back
to .xlsx, and each result is read from the saved file's cached values -- so a
case matches only if our engine computes Excel's value AND writes it the way
Excel reads it.

--baseline makes it a gate: a case the baseline has as "match" that no longer
matches fails the run (exit 1); cases that newly match are listed.

Copyright (C) 2026 Stained Glass OS contributors
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import argparse
import glob
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import corpus  # noqa: E402
from xlsxw import ref  # noqa: E402

ERR_CODE = dict(corpus.ERRORS)       # "#DIV/0!" -> the LibreOffice code corpus.judge() expects
ERR_CODE.update({"#SPILL!": 0, "#CALC!": 0, "#GETTING_DATA": 0, "#FIELD!": 0, "#BLOCKED!": 0,
                 "#CONNECT!": 0, "#BUSY!": 0, "#UNKNOWN!": 0, "#PYTHON!": 0})


def run_docbuilder(engine, script_text, timeout=900):
    fd, script = tempfile.mkstemp(suffix=".docbuilder")
    with os.fdopen(fd, "w") as f:
        f.write(script_text)
    try:
        env = dict(os.environ, LD_LIBRARY_PATH=engine)
        p = subprocess.run([os.path.join(engine, "docbuilder"), script], cwd=engine, env=env,
                           capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout + p.stderr
    finally:
        os.unlink(script)


def sheet_xml(z, name):
    wb = z.read("xl/workbook.xml").decode()
    rels = z.read("xl/_rels/workbook.xml.rels").decode()
    m = re.search(r'<sheet [^>]*name="%s"[^>]*r:id="([^"]+)"' % name, wb)
    t = re.search(r'Id="%s"[^>]*Target="([^"]+)"' % m.group(1), rels) or \
        re.search(r'Target="([^"]+)"[^>]*Id="%s"' % m.group(1), rels)
    return z.read("xl/" + t.group(1).lstrip("/").replace("xl/", "")).decode()


def shared_strings(z):
    try:
        xml = z.read("xl/sharedStrings.xml").decode()
    except KeyError:
        return []
    out = []
    for si in re.finditer(r"<si>(.*?)</si>", xml, re.S):
        out.append(html.unescape("".join(re.findall(r"<t[^>]*>(.*?)</t>", si.group(1), re.S))))
    return out


def read_results(path):
    """{A1: got-tuple as corpus.judge() wants it} for every cell of the Tests sheet."""
    z = zipfile.ZipFile(path)
    sst = shared_strings(z)
    xml = sheet_xml(z, "Tests")
    out = {}
    for cm in re.finditer(r'<c r="([A-Z]+[0-9]+)"([^>]*?)(?:/>|>(.*?)</c>)', xml, re.S):
        a1, attrs, body = cm.group(1), cm.group(2), cm.group(3) or ""
        t = re.search(r'\bt="([^"]+)"', attrs)
        t = t.group(1) if t else "n"
        vm = re.search(r"<v[^>]*>(.*?)</v>", body, re.S)
        v = html.unescape(vm.group(1)) if vm else None
        if t == "inlineStr":
            v = html.unescape("".join(re.findall(r"<t[^>]*>(.*?)</t>", body, re.S)))
            out[a1] = ("text", v)
        elif v is None:
            out[a1] = ("empty",)
        elif t == "e":
            out[a1] = ("error", ERR_CODE.get(v, 0) or 1, v)
        elif t == "b":
            out[a1] = ("bool", v == "1", 1.0 if v == "1" else 0.0)
        elif t == "s":
            out[a1] = ("text", sst[int(v)])
        elif t == "str":
            out[a1] = ("text", v)
        else:
            out[a1] = ("num", float(v))
    return out


def run(cases, engine, xlsx, out_xlsx):
    rc, log = run_docbuilder(engine, (
        'builder.OpenFile("%s", "");\n'
        'Api.RecalculateAllFormulas();\n'
        'builder.SaveFile("xlsx", "%s");\n'
        'builder.CloseFile();\n') % (xlsx, out_xlsx))
    if rc != 0 or not os.path.exists(out_xlsx):
        raise SystemExit("engine failed (rc=%d):\n%s" % (rc, log[-2000:]))
    got = read_results(out_xlsx)
    exported = corpus.read_exported(out_xlsx)
    for c in cases:
        r, col = c.cell
        if c.spill:
            nr, nc = c.spill
            c.got = [[got.get(ref(r + i, col + j), ("empty",)) for j in range(nc)] for i in range(nr)]
        else:
            c.got = got.get(ref(r, col), ("empty",))
        # corpus.judge() treats a formula still carrying "_xlfn." as unknown to
        # LibreOffice; our engine writes known newer functions with the prefix
        # as Excel does, so unknown functions are recognised by #NAME? instead.
        c.lo_formula = ""
        c.exported, c.saved_error = exported.get(ref(r, col), (None, None))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", required=True)
    ap.add_argument("--json")
    ap.add_argument("--report")
    ap.add_argument("--baseline")
    ap.add_argument("--only")
    ap.add_argument("--keep", help="keep the built and saved workbooks here")
    ap.add_argument("--label", default="SG Office engine")
    a = ap.parse_args()
    engine = os.path.abspath(a.engine)
    cases = corpus.load_corpus(sorted(glob.glob(os.path.join(HERE, "corpus-*.txt"))))
    if a.only:
        cases = [c for c in cases if re.search(a.only, c.id)]
    work = tempfile.mkdtemp(prefix="sgoffice-corpus-")
    try:
        xlsx, out = os.path.join(work, "corpus.xlsx"), os.path.join(work, "corpus-saved.xlsx")
        corpus.build(cases, xlsx)
        run(cases, engine, xlsx, out)
        for c in cases:
            corpus.judge(c)
        if a.keep:
            os.makedirs(a.keep, exist_ok=True)
            shutil.copy(xlsx, a.keep)
            shutil.copy(out, a.keep)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    res = {c.id: {"name": c.name, "category": c.category, "file": c.file_formula,
                  "expected": corpus.describe_expected(c.expected), "got": c.shown,
                  "status": c.status, "note": c.note} for c in cases}
    counts = {}
    for c in cases:
        counts[c.status] = counts.get(c.status, 0) + 1
    print("corpus: %d cases, %d functions; results: %s" % (
        len(cases), len({c.name for c in cases}), ", ".join("%s %d" % kv for kv in sorted(counts.items()))))
    if a.json:
        json.dump(res, open(a.json, "w"), indent=1, ensure_ascii=False, sort_keys=True)
    if a.report:
        import report
        report.write(a.report, cases, a.label)
    if a.baseline:
        if not os.path.exists(a.baseline):
            json.dump(res, open(a.baseline, "w"), indent=1, ensure_ascii=False, sort_keys=True)
            print("wrote new baseline %s" % a.baseline)
            return 0
        base = json.load(open(a.baseline))
        regress = [k for k, v in base.items() if v["status"] == "match" and res.get(k, {}).get("status") != "match"]
        better = [k for k, v in res.items() if v["status"] == "match" and base.get(k, {}).get("status") != "match"]
        for k in regress:
            r = res.get(k, {})
            print("REGRESSION  %s  %s  expected %s, got %s (%s)" % (k, r.get("file"), r.get("expected"),
                                                                    r.get("got"), r.get("status")))
        for k in better:
            print("better      %s  now matches Excel" % k)
        print("baseline: %d regressions, %d improvements" % (len(regress), len(better)))
        return 1 if regress else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
