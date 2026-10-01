#!/usr/bin/env python3
"""SG Office -- broken builds of the package the package gate must refuse.

    mutate_deb.py MODE IN.deb OUT.deb

  nocredit   copyright and NOTICE without ONLYOFFICE's credit and terms
  nodep      Depends without Qt WebEngine
  moved      the engine somewhere the program does not look
  x2tnoexec  the converter not executable (only running it as installed shows)

Copyright (C) 2026 Stained Glass OS contributors
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile


def main(mode, src, dst):
    t = tempfile.mkdtemp(prefix="sgdeb-", dir=os.path.dirname(os.path.abspath(dst)))
    try:
        subprocess.run(["dpkg-deb", "-R", src, t], check=True)
        doc = os.path.join(t, "usr/share/doc/sg-office-editors")
        if mode == "nocredit":
            for f in ("copyright", "NOTICE"):
                p = os.path.join(doc, f)
                s = open(p, encoding="utf-8").read()
                s = re.sub(r"ONLYOFFICE|Ascensio System SIA|Section 7\(.\)|CC BY-SA 4\.0", "", s)
                open(p, "w", encoding="utf-8").write(s)
        elif mode == "nodep":
            p = os.path.join(t, "DEBIAN/control")
            s = open(p, encoding="utf-8").read()
            s = re.sub(r"libqt6webengine\w*6 \([^)]*\),\s*", "", s)
            open(p, "w", encoding="utf-8").write(s)
        elif mode == "moved":
            e = os.path.join(t, "usr/lib/sg-office")
            os.rename(os.path.join(e, "engine"), os.path.join(e, "engine-moved"))
        elif mode == "x2tnoexec":
            os.chmod(os.path.join(t, "usr/lib/sg-office/engine/x2t"), 0o644)
        else:
            sys.exit("mutate_deb.py: unknown mode " + mode)
        # md5sums would give the mutant away to dpkg --verify only; keep it consistent
        subprocess.run(["dpkg-deb", "--root-owner-group", "-Zxz", "-z1", "-b", t, dst], check=True,
                       stdout=subprocess.DEVNULL)
    finally:
        shutil.rmtree(t, ignore_errors=True)


if __name__ == "__main__":
    main(*sys.argv[1:4])
