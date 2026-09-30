#!/usr/bin/env python3
"""Write a damaged copy of the round-trip corpus (ooxmlw.py's), for the gate's mutation test:
src.xlsx loses its merged range and its SUM formula -- what an engine that
dropped them would save. The round-trip gate must fail on it.

    mutate.py CORPUS OUTDIR

Copyright (C) 2026 Stained Glass OS contributors
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import os
import re
import shutil
import sys
import zipfile

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
for f in os.listdir(src):
    shutil.copy(os.path.join(src, f), out)
path = os.path.join(out, "src.xlsx")
with zipfile.ZipFile(os.path.join(src, "src.xlsx")) as zin:
    items = [(i, zin.read(i.filename)) for i in zin.infolist()]
with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zout:
    for info, data in items:
        if info.filename == "xl/worksheets/sheet1.xml":
            x = data.decode()
            x2 = re.sub(r"<mergeCells.*?</mergeCells>", "", x, flags=re.S)
            x2 = re.sub(r'<c r="B4"[^>]*>.*?</c>', "", x2, flags=re.S)
            assert x2 != x, "nothing to damage"
            data = x2.encode()
        zout.writestr(info, data)
print("damaged corpus: %s" % out)
