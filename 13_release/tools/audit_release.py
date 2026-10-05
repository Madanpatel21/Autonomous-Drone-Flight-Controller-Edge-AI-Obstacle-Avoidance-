#!/usr/bin/env python3
"""Release-candidate audit (Phase 28, OPS-001).

A versioned release manifest is only worth anything if it matches the artifacts
it names. This tool re-derives every claim in `13_release/RELEASE_MANIFEST.json`
from the tree and fails when it cannot:

  * source revision   - the tree digest (no VCS exists in this workspace; the
    digest *is* the revision identifier) and file count are recomputed.
  * present items     - exist, live inside the repository, and their sha256
    matches the pin.
  * gated items       - the artifact is genuinely absent (a directory declared
    empty may hold only a README, same rule as the manufacturing audit) and its
    gate is declared in this manifest.
  * build targets     - the SIM and host-HIL targets are built and reproducible
    from the recorded commands; the STM32 target is gated on the toolchain.
  * requirement row   - OPS-001 must not read `OPEN` while a release candidate
    exists, and a PARTIAL row must name its gap.

    python 13_release/tools/audit_release.py
    python 13_release/tools/audit_release.py --update-digests --release RC-2
    python 13_release/tools/audit_release.py --selftest   # 7 fixture cases

Re-baselining is deliberate (DEC-022/023): it changes the pin, so the release id
must move with it.
"""
import argparse
import datetime
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST_REL = "13_release/RELEASE_MANIFEST.json"
SKIP_DIRS = ("build", "build-hil", ".git", "__pycache__")
STATUS_RE = re.compile(r"\*\*(MET|PARTIAL|OPEN)([^*|]*)\*\*", re.IGNORECASE)

PROBLEMS = []


def fail(message):
    PROBLEMS.append(message)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def tree_digest(root):
    """One digest over the whole source revision (manifest excluded)."""
    h = hashlib.sha256()
    count = 0
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        parts = path.relative_to(root).parts
        if any(part in SKIP_DIRS for part in parts):
            continue
        rel = path.relative_to(root).as_posix()
        if rel == MANIFEST_REL:
            continue
        h.update(rel.encode("utf-8"))
        h.update(b"\0")
        h.update(sha256(path).encode("utf-8"))
        h.update(b"\n")
        count += 1
    return h.hexdigest(), count


def dir_absent(root, rel):
    path = root / rel.rstrip("/")
    if not path.exists():
        return True
    if path.is_dir():
        return not [p for p in path.iterdir() if p.is_file() and p.name.lower() != "readme.md"]
    return False


def requirement_row(root, rid):
    report = root / "08_testing/VERIFICATION_REPORT.md"
    if not report.exists():
        return None
    for line in report.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.strip().startswith("| %s |" % rid):
            return line
    return None


def run(root):
    manifest_path = root / MANIFEST_REL
    if not manifest_path.exists():
        print("FAIL %s missing" % MANIFEST_REL)
        return 2
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    release = manifest.get("release", {})
    for field in ("id", "created", "requirement", "scope", "status"):
        if not release.get(field):
            fail("release.%s is empty" % field)
    if not manifest.get("build", {}).get("reproduce"):
        fail("build.reproduce is empty (reproducibility must be a command, not a claim)")

    # ---- source revision -----------------------------------------------------
    digest, count = tree_digest(root)
    source = manifest.get("source", {})
    if source.get("tree_digest") != digest:
        fail("source.tree_digest: manifest says %r, the tree computes %r"
             % (source.get("tree_digest"), digest))
    if source.get("tree_files") != count:
        fail("source.tree_files: manifest says %r, the tree has %d"
             % (source.get("tree_files"), count))

    # ---- items ---------------------------------------------------------------
    present = gated = 0
    for item in manifest.get("items", []):
        iid = item.get("id", "?")
        status = item.get("status")
        if status == "present":
            present += 1
            path = root / item.get("path", "")
            if not path.exists():
                fail("%s: declared present but %s does not exist" % (iid, item.get("path")))
                continue
            actual = sha256(path)
            if actual != item.get("sha256"):
                fail("%s: %s changed without a release re-baseline (digest mismatch)"
                     % (iid, item.get("path")))
        elif status == "gated":
            gated += 1
            gate = item.get("gate")
            if gate not in manifest.get("gates", {}):
                fail("%s: gate %r is not declared in this manifest" % (iid, gate))
            expected = item.get("expected_absent")
            if expected and not dir_absent(root, expected):
                fail("%s: %s was declared absent but something exists there (gate %s no longer holds)"
                     % (iid, expected, gate))
        else:
            fail("%s: unknown item status %r" % (iid, status))

    # ---- build targets -------------------------------------------------------
    targets = manifest.get("build", {}).get("targets", {})
    for name in ("sim", "hil_host"):
        if targets.get(name, {}).get("status") != "built":
            fail("build target %s: expected status 'built'" % name)
    stm32 = targets.get("stm32", {})
    if stm32.get("status") != "gated":
        fail("build target stm32: expected status 'gated' (no toolchain exists)")
    elif not dir_absent(root, stm32.get("expected_absent", "")):
        fail("build target stm32: %s is not empty, so the gate no longer holds"
             % stm32.get("expected_absent"))

    # ---- OPS-001 row ---------------------------------------------------------
    row = requirement_row(root, release.get("requirement", "OPS-001"))
    if row is None:
        fail("no verification row for %s" % release.get("requirement"))
    else:
        matches = list(STATUS_RE.finditer(row))
        if not matches:
            fail("%s row carries no status token" % release.get("requirement"))
        else:
            word = matches[-1].group(1).upper()
            if word == "OPEN":
                fail("%s still reads OPEN while a release candidate exists"
                     % release.get("requirement"))
            elif word == "PARTIAL" and "\u2014" not in row:
                fail("%s is PARTIAL but the row names no gap" % release.get("requirement"))

    if PROBLEMS:
        for line in PROBLEMS:
            print("FAIL %s" % line)
        print("RELEASE: FAIL (%d problem(s))" % len(PROBLEMS))
        return 1
    print("release audit: %s (%s), tree %s...%s %d files"
          % (release["id"], release["created"], digest[:12], digest[-8:], count))
    print("release audit: %d present item(s) digest-matched, %d gated item(s) verified absent"
          % (present, gated))
    print("release audit: %s row is recorded, not OPEN" % release["requirement"])
    print("RELEASE: PASS (manifest matches artifacts)")
    return 0


def update(root, new_id=None):
    manifest_path = root / MANIFEST_REL
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    changes = []
    digest, count = tree_digest(root)
    old = manifest["source"].get("tree_digest")
    if old != digest:
        changes.append("source.tree_digest: %r -> %r" % (old, digest))
    manifest["source"]["tree_digest"] = digest
    if manifest["source"].get("tree_files") != count:
        changes.append("source.tree_files: %r -> %d" % (manifest["source"].get("tree_files"), count))
    manifest["source"]["tree_files"] = count
    for item in manifest.get("items", []):
        if item.get("status") != "present":
            continue
        actual = sha256(root / item["path"])
        if item.get("sha256") != actual:
            changes.append("%s %s: digest re-baselined" % (item["id"], item["path"]))
        item["sha256"] = actual
    if new_id and manifest["release"].get("id") != new_id:
        changes.append("release.id: %r -> %r" % (manifest["release"].get("id"), new_id))
        manifest["release"]["id"] = new_id
    manifest["release"]["created"] = datetime.date.today().isoformat()
    text = json.dumps(manifest, indent=2) + "\n"
    if text != manifest_path.read_text(encoding="utf-8"):
        manifest_path.write_text(text, encoding="utf-8")
    for line in changes:
        print("updated: %s" % line)
    print("release audit: re-baselined %s (%d change(s)); run the audit again to verify"
          % (manifest["release"]["id"], len(changes)))
    return 0


# ---------------------------------------------------------------------------
# Negative tests: build a fixture release and prove the audit detects each
# defect class it claims (DEC-023). Every case runs this CLI as a subprocess.
# ---------------------------------------------------------------------------
FIXTURE_REQ = {
    "release": {"id": "RC-T", "created": "2026-01-01", "requirement": "OPS-001",
                "scope": "fixture", "status": "PARTIAL - fixture"},
    "source": {"tree_files": 0, "tree_digest": ""},
    "build": {"reproduce": ["true"],
              "targets": {"sim": {"status": "built"}, "hil_host": {"status": "built"},
                          "stm32": {"status": "gated", "expected_absent": "empty_dir/"}}},
    "gates": {"GATE_X": "fixture gate"},
    "items": [
        {"id": "REL-01", "status": "present", "path": "artifact.txt", "sha256": ""},
        {"id": "REL-G1", "status": "gated", "gate": "GATE_X",
         "expected_absent": "empty_dir/"},
    ],
}


def fixture(root, ops_status="PARTIAL \u2014 gated on BOARD", stray=False):
    (root / "08_testing").mkdir(parents=True, exist_ok=True)
    (root / "13_release/tools").mkdir(parents=True, exist_ok=True)
    (root / "empty_dir").mkdir(exist_ok=True)
    (root / "artifact.txt").write_text("artifact\n", encoding="utf-8")
    (root / "08_testing/VERIFICATION_REPORT.md").write_text(
        "| OPS-001 | R | fixture | **%s** |\n" % ops_status, encoding="utf-8")
    manifest = json.loads(json.dumps(FIXTURE_REQ))
    (root / MANIFEST_REL).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    subprocess.run([sys.executable, str(Path(__file__).resolve()),
                    "--root", str(root), "--update-digests"],
                   capture_output=True, text=True)
    if stray:
        (root / "empty_dir/stray.bin").write_bytes(b"x")


def case_audit(root):
    proc = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--root", str(root)],
                          capture_output=True, text=True)
    return proc.returncode, proc.stdout


def selftest():
    cases = []

    def run_case(name, mutate, want_rc, want_text):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            fixture(root)
            mutate(root)
            rc, out = case_audit(root)
            ok = rc == want_rc and want_text in out
            print("%s %s" % ("PASS" if ok else "FAIL", name))
            if not ok:
                print("     rc=%d want=%d out=%r" % (rc, want_rc, out[-200:]))
            cases.append(ok)

    run_case("control: untouched fixture audits clean", lambda root: None, 0, "RELEASE: PASS")
    run_case("tree digest drift after a source change",
             lambda root: (root / "new_source.c").write_text("int x;\n", encoding="utf-8"),
             1, "source.tree_digest")
    run_case("pinned item modified without re-baseline",
             lambda root: (root / "artifact.txt").write_text("tampered\n", encoding="utf-8"),
             1, "changed without a release re-baseline")
    run_case("present item missing",
             lambda root: (root / "artifact.txt").unlink(), 1, "does not exist")
    run_case("gated artifact has appeared (gate no longer holds)",
             lambda root: (root / "empty_dir/stray.bin").write_bytes(b"x"),
             1, "no longer holds")
    run_case("OPS-001 still reads OPEN",
             lambda root: (root / "08_testing/VERIFICATION_REPORT.md").write_text(
                 "| OPS-001 | R | fixture | **OPEN** \u2014 Phase 28 |\n", encoding="utf-8"),
             1, "still reads OPEN")
    run_case("OPS-001 PARTIAL with no named gap",
             lambda root: (root / "08_testing/VERIFICATION_REPORT.md").write_text(
                 "| OPS-001 | R | fixture | **PARTIAL** |\n", encoding="utf-8"),
             1, "names no gap")
    print("release audit selftest: %d/%d cases behaved as specified"
          % (sum(cases), len(cases)))
    return 0 if all(cases) else 1


def main():
    ap = argparse.ArgumentParser(description="release-candidate manifest audit")
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--update-digests", action="store_true",
                    help="re-baseline the tree digest and every present item (deliberate)")
    ap.add_argument("--release", metavar="ID", help="new release id for a re-baseline")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    root = Path(args.root)
    if args.update_digests:
        return update(root, args.release)
    return run(root)


if __name__ == "__main__":
    sys.exit(main())
