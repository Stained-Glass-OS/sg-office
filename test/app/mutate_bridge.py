#!/usr/bin/env python3
"""Write a damaged copy of app/res/bridge.js, for the program gate's mutation
test -- each must make test/app/app_check.py fail:

  nochanges   the editor's changes never reach the program (saves lose edits)
  fakesave    Save reports success without writing anything
  notheme     SG Office's theme is not offered (the stock look shows)

    mutate_bridge.py NAME OUT.js

Copyright (C) 2026 Stained Glass OS contributors
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import os
import re
import sys

name, out = sys.argv[1], sys.argv[2]
src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "app", "res", "bridge.js")
s = open(src).read()
if name == "nochanges":
    s2 = s.replace('callSync("changes", null,', 'void (null,')
elif name == "fakesave":
    s2 = re.sub(r'(LocalFileSave: function \([^)]*\) \{)',
                r'\1 window.DesktopOfflineAppDocumentEndSave(0); return;', s, count=1)
elif name == "notheme":
    s2 = s.replace('localthemes: (function', 'localthemes_off: (function')
else:
    sys.exit("unknown mutant " + name)
assert s2 != s, "mutant %s changed nothing" % name
open(out, "w").write(s2)
print("mutant bridge %s: %s" % (name, out))
