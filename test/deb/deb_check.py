#!/usr/bin/env python3
"""SG Office -- the package's gate: the built sg-office-editors .deb, installed.

    deb_check.py --deb FILE --work DIR [--buildroot DIR] [--no-run]

  * contents: the program, the engine and the editors where the program looks
    for them (/usr/bin, /usr/lib/sg-office/engine, /usr/share/sg-office), and
    the licence: debian/copyright and NOTICE credit ONLYOFFICE (Ascensio
    System SIA, AGPL-3.0 with its Section 7 terms) and the third-party lists
  * dependencies: every library an executable or library in the package
    needs is either in the package or in a package its Depends names
  * installed: dpkg installs it into a scratch system root (a copy of the
    trixie build root, in a user namespace: no privileges, nothing on the
    host changes) and the program's gate (test/app/app_check.py --sysroot)
    runs /usr/bin/sg-office there, as installed: open, edit, save, Save As

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

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
# the user namespace the scratch root lives in: root there is the build
# root's owner (the first subordinate id), the caller is 65536
NS = ["unshare", "--map-users=0:100000:65536", "--map-groups=0:100000:65536",
      "--map-users=65536:%d:1" % os.getuid(), "--map-groups=65536:%d:1" % os.getgid(),
      "--setuid", "0", "--setgid", "0", "--mount", "--fork"]
DOC = "usr/share/doc/sg-office-editors/"
NEED_FILES = ["usr/bin/sg-office", "usr/lib/sg-office/engine/x2t", "usr/lib/sg-office/engine/libdoctrenderer.so",
              "usr/lib/sg-office/engine/empty/new.docx", "usr/lib/sg-office/engine/DoctRenderer.config",
              "usr/share/sg-office/sdkjs/word/sdk-all.js", "usr/share/sg-office/web-apps/apps/api/documents/api.js",
              DOC + "copyright", DOC + "NOTICE", DOC + "3DPARTY-core.md", DOC + "3DPARTY-sdkjs.md",
              DOC + "3DPARTY-web-apps.md", DOC + "upstream.conf"]
CREDIT = [r"ONLYOFFICE", r"Ascensio System SIA", r"GNU AFFERO GENERAL PUBLIC LICENSE", r"Section 7\(b\)",
          r"Section 7\(e\)", r"CC BY-SA 4\.0", r"Section 13"]


def field(deb, name):
    return subprocess.run(["dpkg-deb", "-f", deb, name], capture_output=True, text=True).stdout.strip()


def elfs(root):
    for d, _, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            with open(p, "rb") as fh:
                if fh.read(4) == b"\x7fELF":
                    yield p


def needed(path):
    out = subprocess.run(["readelf", "-d", path], capture_output=True, text=True).stdout
    return re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", out)


def owner(lib, root):
    """The package that has LIB in the system root ROOT (the build root the
    program was built in and is installed into below), or None."""
    db = os.path.join(root, "var/lib/dpkg")
    for d in ("/usr/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu", "/usr/lib", "/lib", "/lib64"):
        if not os.path.exists(root + d + "/" + lib):
            continue
        for p in (d + "/" + lib, ("/usr" + d if not d.startswith("/usr/") else d[4:]) + "/" + lib):
            r = subprocess.run(["dpkg", "--admindir=" + db, "-S", p], capture_output=True, text=True)
            if r.returncode == 0:
                return r.stdout.split(":")[0].strip()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deb", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--buildroot", default="/var/tmp/sgoffice/root-build")
    ap.add_argument("--no-run", action="store_true")
    a = ap.parse_args()
    deb, work = os.path.abspath(a.deb), os.path.abspath(a.work)
    fails = []

    def check(ok, what):
        print("   %s %s" % ("PASS" if ok else "FAIL", what))
        if not ok:
            fails.append(what)

    # a previous run's scratch root belongs to the namespace's root: removed there
    if os.path.exists(work):
        subprocess.run(NS + ["rm", "-rf", work], check=False)
    os.makedirs(work)
    x = os.path.join(work, "x")
    subprocess.run(["dpkg-deb", "-x", deb, x], check=True)
    print("\n[contents] %s %s" % (field(deb, "Package"), field(deb, "Version")))
    check(field(deb, "Package") == "sg-office-editors", "the package is sg-office-editors")
    for f in NEED_FILES:
        check(os.path.exists(os.path.join(x, f)), "has /" + f)
    sdk = os.path.join(x, "usr/lib/sg-office/engine/sdkjs")
    check(os.path.islink(sdk) and os.path.realpath(sdk) == os.path.realpath(os.path.join(x, "usr/share/sg-office/sdkjs")),
          "the engine's sdkjs is the editors' (/usr/share/sg-office/sdkjs)")
    try:
        text = open(os.path.join(x, DOC + "copyright"), encoding="utf-8").read() + \
               open(os.path.join(x, DOC + "NOTICE"), encoding="utf-8").read()
    except OSError:
        text = ""
    for c in CREDIT:
        check(re.search(c, text) is not None, "copyright/NOTICE: %s" % c.replace("\\", ""))

    print("\n[dependencies]")
    depends = field(deb, "Depends")
    names = {re.sub(r"[\s(].*", "", alt.strip()) for d in depends.split(",") for alt in d.split("|")}
    for must in ("libqt6webenginecore6", "libnode115", "libicu76"):
        check(must in names, "Depends names %s" % must)
    inside = {os.path.basename(p) for p in elfs(x)} | {f for d, _, fs in os.walk(x) for f in fs}
    missing = {}
    for p in elfs(x):
        for lib in needed(p):
            if lib in inside:
                continue
            pkg = owner(lib, a.buildroot)
            if pkg not in names:
                missing.setdefault(lib, (pkg, os.path.relpath(p, x)))
    for lib, (pkg, user) in sorted(missing.items()):
        check(False, "%s (needed by /%s) is in %s, which Depends does not name" % (lib, user, pkg or "no package"))
    check(not missing, "every library the package's programs need is in it or in its Depends")

    if not a.no_run:
        print("\n[installed] dpkg -i into a scratch root on %s" % a.buildroot)
        sysroot = os.path.join(work, "root")
        script = r"""set -e
export PATH=/usr/local/sbin:/usr/sbin:/sbin:/usr/local/bin:/usr/bin:/bin
# a copy of the build root (an overlay on it refuses dpkg's renames: EXDEV)
cp -a "$B" "$W/root"
cp "$DEB" "$W/root/tmp/sg-office-editors.deb"
/usr/sbin/chroot "$W/root" dpkg -i /tmp/sg-office-editors.deb > "$W/dpkg.log" 2>&1 || { cat "$W/dpkg.log"; exit 3; }
/usr/sbin/chroot "$W/root" dpkg-query -W -f='${Status}\n' sg-office-editors | grep -q 'install ok installed' || exit 4
exec python3 "$APPCHECK" --sysroot "$W/root" --work "$W/app"
"""
        env = dict(os.environ, W=work, B=a.buildroot, DEB=deb, APPCHECK=os.path.join(REPO, "test", "app", "app_check.py"))
        r = subprocess.run(NS + ["sh", "-c", script], env=env, capture_output=True, text=True)
        print(r.stdout + r.stderr)
        check(r.returncode != 3, "dpkg installed the package (its dependencies satisfied)")
        check(r.returncode == 0, "the program's gate passed with the package as installed")
        # the scratch root is 3 GB: removed (as the namespace's root owns it)
        subprocess.run(NS + ["rm", "-rf", sysroot], check=False)

    print("\n=== SG Office package gate: %s ===" % ("PASS" if not fails else "FAIL (%d)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
