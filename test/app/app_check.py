#!/usr/bin/env python3
"""SG Office -- the program's gate: open, edit, save, Save As, the look.

    app_check.py --root ROOT --work DIR [--bridge FILE] [--only KIND] [--no-odf]
    app_check.py --sysroot DIR --work DIR ...    the program as installed in DIR

For each of a .docx, .xlsx and .pptx (test/roundtrip/ooxmlw.py's), SG Office
runs headlessly (test/app/run.sh: the build root, its own Xvfb, a scratch
HOME) and is driven the way a person uses it -- a click into the document,
End, typing, Save -- by SG_OFFICE_AUTOPILOT. Then:

  * the saved file has the typed text, and every feature the round-trip gate
    checks (test/roundtrip/roundtrip.py) is still there
  * the program's header is its SG Office colour (the theme is ours)
  * Save As to OpenDocument (.odt/.ods/.odp) writes a valid package with
    the typed text in it

  * an empty .docx (File Explorer's New > Document) opens as a new document
    and saves back to that file

Exit 0 when all hold. --bridge runs with another bridge.js (the mutants).

Copyright (C) 2026 Stained Glass OS contributors
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "test", "roundtrip"))
import ooxmlw      # noqa: E402
import roundtrip   # noqa: E402

MARK = "SGEDIT42"
# where to click in each editor (fractions of the editor view): into the text
KINDS = {
    "docx": {"click": "0.5,0.45", "keys": "key:End", "colour": "#2f6fd8", "odf": "odt",
             "odf_mime": "application/vnd.oasis.opendocument.text", "check": roundtrip.check_docx},
    "xlsx": {"click": "0.15,0.45", "keys": "", "colour": "#239a5e", "odf": "ods",
             "odf_mime": "application/vnd.oasis.opendocument.spreadsheet", "check": roundtrip.check_xlsx},
    "pptx": {"click": "0.344,0.416", "keys": "key:End", "colour": "#c84b16", "odf": "odp",
             "odf_mime": "application/vnd.oasis.opendocument.presentation", "check": roundtrip.check_pptx},
}


def text_of(path):
    """Every XML part's text of an OOXML/ODF package, concatenated."""
    with zipfile.ZipFile(path) as z:
        return "".join(re.sub(r"<[^>]+>", "", z.read(n).decode("utf-8", "replace"))
                       for n in z.namelist() if n.endswith(".xml"))


SYSROOT = {}


def run(root, work, doc, autopilot, env_extra, bridge):
    env = dict(os.environ, **SYSROOT)
    env.update(SG_OFFICE_AUTOPILOT=autopilot, SG_OFFICE_LOG="1", SG_TIMEOUT="150", **env_extra)
    if bridge:
        env["SG_OFFICE_BRIDGE"] = bridge
    p = subprocess.run(["sh", os.path.join(HERE, "run.sh"), root, work, doc], env=env,
                       capture_output=True, text=True)
    log = p.stdout + p.stderr
    with open(os.path.join(work, "run.log"), "w") as f:
        f.write(log)
    return p.returncode, log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root")
    ap.add_argument("--sysroot")
    ap.add_argument("--work", required=True)
    ap.add_argument("--bridge")
    ap.add_argument("--only")
    ap.add_argument("--no-odf", action="store_true")
    a = ap.parse_args()
    if not a.root and not a.sysroot:
        ap.error("--root or --sysroot")
    if a.sysroot:
        SYSROOT["SG_OFFICE_SYSROOT"] = os.path.abspath(a.sysroot)
    root, work = os.path.abspath(a.root or a.sysroot), os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    corpus = os.path.join(work, "corpus")
    os.makedirs(corpus)
    ooxmlw.docx(os.path.join(corpus, "src.docx"))
    ooxmlw.xlsx(os.path.join(corpus, "src.xlsx"))
    ooxmlw.pptx(os.path.join(corpus, "src.pptx"))

    fails = []

    def check(ok, what):
        print("   %s %s" % ("PASS" if ok else "FAIL", what))
        if not ok:
            fails.append(what)

    for ext, k in KINDS.items():
        if a.only and ext != a.only:
            continue
        click = "click:%s;%s" % (k["click"], (k["keys"] + ";") if k["keys"] else "")
        commit = ";key:Return" if ext == "xlsx" else ""
        # -- edit and save ----------------------------------------------------------------
        w = os.path.join(work, ext)
        os.makedirs(w)
        doc = os.path.join(w, "doc." + ext)
        shutil.copy(os.path.join(corpus, "src." + ext), doc)
        # the .docx is opened through a linked folder, as files opened from the
        # Windows side arrive (the prefix's dosdevices/c: link)
        arg = doc
        if ext == "docx":
            os.symlink(w, os.path.join(work, "linked"))
            arg = os.path.join(work, "linked", os.path.basename(doc))
        rc, log = run(root, w, arg, click + "type:" + MARK + commit + ";wait:1500;pixel:1000,45;save;wait:3000;quit", {}, a.bridge)
        print("\n[%s] edit + save (rc=%d)" % (ext, rc))
        check(rc == 0, "%s: the program ran and quit" % ext)
        check(("saved " + os.path.realpath(doc)) in log, "%s: the program saved the document (at its real path)" % ext)
        try:
            check(MARK in text_of(doc), "%s: the saved file has the typed text" % ext)
            for name, ok in k["check"](doc):
                check(ok, name + " (after editing)")
        except Exception as e:           # a damaged file
            check(False, "%s: saved file readable (%s)" % (ext, e))
        m = re.search(r"pixel 1000,45 (#[0-9a-f]{6})", log)
        check(bool(m) and m.group(1) == k["colour"],
              "%s: header is SG Office's %s (got %s)" % (ext, k["colour"], m.group(1) if m else "nothing"))
        if a.no_odf:
            continue
        # -- Save As OpenDocument ---------------------------------------------------------------
        w2 = os.path.join(work, ext + "-odf")
        os.makedirs(w2)
        doc2 = os.path.join(w2, "doc." + ext)
        shutil.copy(os.path.join(corpus, "src." + ext), doc2)
        target = os.path.join(w2, "as." + k["odf"])
        rc, log = run(root, w2, doc2, click + "type:" + MARK + commit + ";wait:1500;saveas;wait:4000;quit",
                      {"SG_OFFICE_SAVE_AS": target}, a.bridge)
        print("\n[%s] Save As .%s (rc=%d)" % (ext, k["odf"], rc))
        check(os.path.isfile(target), "%s: Save As wrote %s" % (ext, os.path.basename(target)))
        if os.path.isfile(target):
            with zipfile.ZipFile(target) as z:
                check(z.read("mimetype").decode() == k["odf_mime"], "%s: it is %s" % (ext, k["odf_mime"]))
            check(MARK in text_of(target), "%s: it has the typed text" % ext)
        with zipfile.ZipFile(os.path.join(corpus, "src." + ext)) as a0, zipfile.ZipFile(doc2) as a1:
            check(sorted(a0.namelist()) == sorted(a1.namelist()) and all(a0.read(n) == a1.read(n) for n in a0.namelist()),
                  "%s: Save As left the original untouched" % ext)

    # -- an empty file opens as a new document --------------------------------------------
    if not a.only or a.only == "docx":
        k = KINDS["docx"]
        w = os.path.join(work, "empty")
        os.makedirs(w)
        doc = os.path.join(w, "New Document.docx")
        open(doc, "wb").close()
        rc, log = run(root, w, doc, "click:%s;%s;type:%s;wait:1500;save;wait:3000;quit" % (k["click"], k["keys"], MARK),
                      {}, a.bridge)
        print("\n[empty .docx] new document + save (rc=%d)" % rc)
        check(rc == 0, "empty .docx: the program ran and quit")
        try:
            check(MARK in text_of(doc), "empty .docx: saved back as a document with the typed text")
        except Exception as e:
            check(False, "empty .docx: saved file readable (%s)" % e)

    print("\n=== SG Office program gate: %s ===" % ("PASS" if not fails else "FAIL (%d)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
