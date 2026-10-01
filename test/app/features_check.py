#!/usr/bin/env python3
"""SG Office -- the program's second gate: what a person does beyond typing
and saving, each driven headlessly as app_check.py drives the editor
(test/app/run.sh: the build root, its own Xvfb, a scratch HOME).

    features_check.py --root ROOT --work DIR [--bridge FILE] [--only NAME,...]

  csv       a CSV with semicolons and accented names (as a comma-decimal
            locale writes them) opens, and saves back with its semicolons
  export    File > Download as PDF writes a PDF but leaves the document
            unsaved: its window still holds the change, the file does not
  print     File > Print hands lp a PDF with the editor's choices (printer,
            copies, both sides); Quick Print goes to the default printer;
            neither saves the document
  fonts     the font tables are made at the first start only: a second start
            with the same fonts does not make them again
  altf4     Alt+F4 closes the window
  units     a US English account measures in inches, a German one in cm
  title     after Save As, the editor's title is the new file's name
  handoff   a second start hands its file to the SG Office already running
            (one process, a window more); a file already open is not opened
            twice
  dark      in the session's dark look the title bar and the editor are dark
  recents   an opened file is in the editor's File > Open Recent
  taskbar   each program's window has its own X class
            (sg-office-documents), and the icons the taskbar shows for
            those classes are in the Wine profile's Linux app icons folder
  closeglyph the title bar's close cross is as dark as its neighbours

Exit 0 when all hold.

Copyright (C) 2026 Stained Glass OS contributors
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "test", "roundtrip"))
import ooxmlw      # noqa: E402

MARK = "SGEDIT42"
DOC_CLICK = "click:0.5,0.45;key:End;"
CELL_CLICK = "click:0.15,0.45;"
ALL = ["csv", "export", "print", "fonts", "altf4", "units", "title", "handoff", "dark", "recents", "taskbar", "closeglyph"]
OPT = {}


def run(work, args, autopilot, env_extra=None, timeout="150"):
    env = dict(os.environ)
    env.update(SG_OFFICE_AUTOPILOT=autopilot, SG_OFFICE_LOG="1", SG_TIMEOUT=timeout, **(env_extra or {}))
    if OPT.get("bridge"):
        env["SG_OFFICE_BRIDGE"] = OPT["bridge"]
    t0 = time.time()
    p = subprocess.run(["sh", os.path.join(HERE, "run.sh"), OPT["root"], work] + list(args), env=env,
                       capture_output=True, text=True)
    log = p.stdout + p.stderr
    with open(os.path.join(work, "run-%d.log" % int(t0 * 1000)), "w") as f:
        f.write(log)
    return p.returncode, log, time.time() - t0


def text_of(path):
    with zipfile.ZipFile(path) as z:
        return "".join(re.sub(r"<[^>]+>", "", z.read(n).decode("utf-8", "replace"))
                       for n in z.namelist() if n.endswith(".xml"))


def fresh(work, name):
    d = os.path.join(work, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def docx_in(d, name="doc.docx"):
    path = os.path.join(d, name)
    ooxmlw.docx(path)
    return path


def evals(log):
    return re.findall(r"sg-office: eval (.*)", log)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--bridge")
    ap.add_argument("--only")
    a = ap.parse_args()
    OPT["root"] = os.path.abspath(a.root)
    OPT["bridge"] = a.bridge and os.path.abspath(a.bridge)
    work = os.path.abspath(a.work)
    os.makedirs(work, exist_ok=True)
    only = a.only.split(",") if a.only else ALL
    fails = []

    def check(ok, what):
        print("   %s %s" % ("PASS" if ok else "FAIL", what))
        if not ok:
            fails.append(what)

    if "csv" in only:
        d = fresh(work, "csv")
        csv = os.path.join(d, "contacts.csv")
        with open(csv, "wb") as f:
            f.write("Name;City;Amount\n\"Smith; Jane\";Zürich;1,5\nJosé Núñez;Malmö;2,25\n".encode("utf-8"))
        rc, log, _ = run(d, [csv], CELL_CLICK + "type:" + MARK + ";key:Return;wait:1500;save;wait:3000;quit")
        print("\n[csv] open, type, save (rc=%d)" % rc)
        check("sg-office: ready" in log and "x2t could not open" not in log, "csv: the CSV opened")
        out = open(csv, encoding="utf-8", errors="replace").read()
        check(MARK in out, "csv: the typed text was saved into it")
        check("Zürich" in out and "Núñez" in out, "csv: accented names kept (read as UTF-8)")
        check(out.splitlines()[0].count(";") == 2 and "," not in out.splitlines()[0],
              "csv: saved back with its semicolons (%r)" % out.splitlines()[0])

    if "export" in only:
        d = fresh(work, "export")
        doc = docx_in(d)
        before = open(doc, "rb").read()
        pdf = os.path.join(d, "copy.pdf")
        rc, log, _ = run(d, [doc], DOC_CLICK + "type:" + MARK + ";wait:1500;exportpdf;wait:5000;state;quit",
                         {"SG_OFFICE_SAVE_AS": pdf})
        print("\n[export] Download as PDF (rc=%d)" % rc)
        check(os.path.isfile(pdf) and open(pdf, "rb").read(5) == b"%PDF-", "export: the PDF was written")
        check("state modified=1" in log, "export: the document is still modified afterwards (%s)"
              % (re.findall(r"state modified=\d", log) or "no state"))
        check(open(doc, "rb").read() == before, "export: the document's own file is untouched")

    if "print" in only:
        d = fresh(work, "print")
        doc = docx_in(d)
        before = open(doc, "rb").read()
        lp = os.path.join(d, "fake-lp")
        with open(lp, "w") as f:
            f.write('#!/bin/sh\nD=$(dirname "$0")\nn=$(ls "$D"/job-*.args 2>/dev/null | wc -l)\n'
                    'printf "%s\\n" "$@" > "$D/job-$n.args"\nfor x; do last=$x; done\ncp "$last" "$D/job-$n.pdf"\n'
                    'echo "request id is Test-$n (1 file(s))"\n')
        os.chmod(lp, 0o755)
        rc, log, _ = run(d, [doc], DOC_CLICK + "type:" + MARK + ";wait:1500;print;wait:6000;print:quick;wait:6000;state;quit",
                         {"SG_OFFICE_PRINTER": "SG-Test-Printer", "SG_OFFICE_LP": lp})
        print("\n[print] File > Print, Quick Print (rc=%d)" % rc)
        jobs = sorted(f for f in os.listdir(d) if f.endswith(".args"))
        check(len(jobs) == 2, "print: two print jobs reached lp (%d)" % len(jobs))
        if jobs:
            args = open(os.path.join(d, jobs[0])).read().split("\n")
            check("SG-Test-Printer" in args and "-d" in args, "print: to the printer chosen (%s)" % " ".join(args))
            check("-n" in args and "2" in args, "print: two copies, as the editor asked")
            check("sides=two-sided-long-edge" in args, "print: on both sides, as the editor asked")
            check(open(os.path.join(d, jobs[0].replace(".args", ".pdf")), "rb").read(5) == b"%PDF-", "print: lp got a PDF")
        if len(jobs) > 1:
            args = open(os.path.join(d, jobs[1])).read().split("\n")
            check("-d" not in args, "print: Quick Print goes to the default printer (%s)" % " ".join(args))
        check("state modified=1" in log, "print: printing did not save the document")
        check(open(doc, "rb").read() == before, "print: the document's own file is untouched")

    if "fonts" in only:
        d = fresh(work, "fonts")
        doc = docx_in(d)
        rc1, log1, t1 = run(d, [doc], "wait:500;quit")
        rc2, log2, t2 = run(d, [doc], "wait:500;quit")
        print("\n[fonts] two starts (rc=%d %d, %.0f s then %.0f s)" % (rc1, rc2, t1, t2))
        check("font tables generated" in log1, "fonts: the first start made the font tables")
        check(rc2 == 0 and "font tables generated" not in log2, "fonts: the second start did not make them again")

    if "altf4" in only:
        d = fresh(work, "altf4")
        doc = docx_in(d)
        rc, log, t = run(d, [doc], "wait:1000;key:Alt+F4", timeout="60")
        print("\n[altf4] (rc=%d, %.0f s)" % (rc, t))
        check(rc == 0, "altf4: Alt+F4 closed the window (the program ended by itself)")

    if "units" in only:
        probe = "eval:String(Common.Utils.Metric.getCurrentMetric())"
        for loc, want, name in (("en_US.UTF-8", "2", "inches"), ("de_DE.UTF-8", "0", "cm")):
            d = fresh(work, "units-" + loc[:2])
            doc = docx_in(d)
            rc, log, _ = run(d, [doc], "wait:1500;" + probe + ";quit", {"SG_TEST_LC_ALL": loc})
            got = evals(log)
            print("\n[units] %s (rc=%d)" % (loc, rc))
            check(got[-1:] == [want], "units: %s measures in %s (metric %s)" % (loc, name, got))

    if "title" in only:
        d = fresh(work, "title")
        doc = docx_in(d)
        target = os.path.join(d, "Renamed report.odt")
        rc, log, _ = run(d, [doc], DOC_CLICK + "type:" + MARK + ";wait:1500;saveas;wait:5000;"
                         "eval:[].map.call(document.querySelectorAll('#title-doc-name'),function(e){return e.value}).join('|');quit",
                         {"SG_OFFICE_SAVE_AS": target})
        print("\n[title] Save As (rc=%d)" % rc)
        check(os.path.isfile(target), "title: Save As wrote the file")
        names = evals(log)[-1].split("|") if evals(log) else []
        check(bool(names) and all(n == "Renamed report.odt" for n in names),
              "title: the editor's title is the new name (%s)" % evals(log))

    if "handoff" in only:
        d = fresh(work, "handoff")
        doc1, doc2 = docx_in(d, "one.docx"), docx_in(d, "two.docx")
        env = dict(os.environ, SG_OFFICE_AUTOPILOT="wait:22000;windows;quitall", SG_OFFICE_LOG="1", SG_TIMEOUT="120")
        if OPT.get("bridge"):
            env["SG_OFFICE_BRIDGE"] = OPT["bridge"]
        logf = os.path.join(d, "primary.log")
        with open(logf, "w") as lf:
            first = subprocess.Popen(["sh", os.path.join(HERE, "run.sh"), OPT["root"], d, doc1], env=env,
                                     stdout=lf, stderr=subprocess.STDOUT)
            deadline = time.time() + 90
            while time.time() < deadline and "sg-office: ready" not in open(logf).read():
                time.sleep(0.5)
            rc2, log2, t2 = run(d, [doc2], "", timeout="40")
            rc3, log3, t3 = run(d, [doc1], "", timeout="40")
            first.wait(timeout=200)
        log1 = open(logf).read()
        print("\n[handoff] primary + two more starts (rc=%d %d %d; %.1f s, %.1f s)" % (first.returncode, rc2, rc3, t2, t3))
        check(rc2 == 0 and "handed to the SG Office already running" in log2, "handoff: the second start handed its file over")
        check(rc3 == 0 and "handed to the SG Office already running" in log3, "handoff: the third start handed its file over")
        m = re.search(r"windows (\d+): (.*)", log1)
        check(bool(m) and m.group(1) == "2" and "two.docx" in m.group(2),
              "handoff: one program, two windows: one.docx and two.docx (%s)" % (m.group(0) if m else "no list"))
        check("already open: " in log1 and "one.docx" in log1.split("already open: ")[-1].split("\n")[0],
              "handoff: one.docx again brought its window forward, no second copy")

    if "dark" in only:
        d = fresh(work, "dark")
        doc = docx_in(d)
        # Settings > Colors' dark mode, as sg-settingsctl writes it for Linux programs
        os.makedirs(os.path.join(d, "home", ".config", "gtk-3.0"), exist_ok=True)
        with open(os.path.join(d, "home", ".config", "gtk-3.0", "settings.ini"), "w") as f:
            f.write("[Settings]\ngtk-application-prefer-dark-theme=true\n")
        rc, log, _ = run(d, [doc], "wait:1500;pixel:600,12;pixel:1000,45;quit")
        px = dict(re.findall(r"pixel (\d+,\d+) #([0-9a-f]{6})", log))
        print("\n[dark] (rc=%d) %s" % (rc, px))

        def lum(h):
            return (int(h[0:2], 16) * 299 + int(h[2:4], 16) * 587 + int(h[4:6], 16) * 114) / 1000 if h else 255
        check(lum(px.get("600,12")) < 60, "dark: the title bar is dark (#%s)" % px.get("600,12"))
        check(lum(px.get("1000,45")) < 90, "dark: the editor's header is dark (#%s)" % px.get("1000,45"))

    if "recents" in only:
        d = fresh(work, "recents")
        doc = docx_in(d, "Budget notes.docx")
        rc, log, _ = run(d, [doc], "wait:2500;eval:Common.Controllers.Desktop.recentFiles().map(function(f){return f.title}).join('|');quit")
        print("\n[recents] (rc=%d) %s" % (rc, evals(log)))
        check(any("Budget notes.docx" in e for e in evals(log)), "recents: the opened file is in File > Open Recent")

    if "taskbar" in only:
        d = fresh(work, "taskbar")
        doc = docx_in(d)
        profile = os.path.join(d, "prefix", "drive_c", "users", "tester")
        os.makedirs(profile)
        rc, log, _ = run(d, [doc], "wait:1000;wmclass;quit", {"WINEPREFIX": os.path.join(d, "prefix"), "USER": "tester"})
        m = re.search(r"wmclass (.*)", log)
        print("\n[taskbar] (rc=%d) %s" % (rc, m.group(0) if m else "no class"))
        check(bool(m) and m.group(1).startswith("sg-office|sg-office-documents|"),
              "taskbar: the window's class is sg-office-documents (%s)" % (m.group(1) if m else None))
        icons = os.path.join(profile, "AppData", "Local", "Stained Glass", "Linux app icons")
        for name in ("sg-office-documents", "sg-office-spreadsheets", "sg-office-presentations"):
            f = os.path.join(icons, name + ".ico")
            head = open(f, "rb").read(6) if os.path.isfile(f) else b""
            check(head[:4] == b"\0\0\1\0" and head[4] >= 4, "taskbar: %s.ico is an icon file (%r)" % (name, head))

    if "closeglyph" in only:
        d = fresh(work, "closeglyph")
        doc = docx_in(d)
        # the window is 1280 wide (5 px resize margin): the close button's
        # cross is centred at (1251, 20); a pixel on its diagonal, and the
        # middle of the minimize bar beside it, drawn in the same colour
        rc, log, _ = run(d, [doc], "wait:1000;pixel:1253,22;pixel:1159,20;quit")
        px = dict(re.findall(r"pixel (\d+,\d+) #([0-9a-f]{6})", log))
        lum = lambda h: (int(h[0:2], 16) * 299 + int(h[2:4], 16) * 587 + int(h[4:6], 16) * 114) / 1000 if h else 255
        print("\n[closeglyph] (rc=%d) %s" % (rc, px))
        check(lum(px.get("1253,22")) <= lum(px.get("1159,20")) + 10,
              "closeglyph: the close cross is as dark as the minimize bar (#%s, #%s)" % (px.get("1253,22"), px.get("1159,20")))

    print("\n=== SG Office features gate: %s ===" % ("PASS" if not fails else "FAIL (%d)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
