#!/usr/bin/env python3
"""Negative tests for audit_release_manifest.py.

An audit that has never failed is a document, not a check. This harness builds a
throwaway copy of the tree, runs the real audit against it as a subprocess, and
asserts that each class of defect the audit claims to detect actually fails the
run - and that an untouched copy passes (the control).

    python 12_manufacturing/tools/selftest_audit.py

Exit 0 = every case behaved as specified.
"""
import csv
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AUDIT = ROOT / "12_manufacturing" / "tools" / "audit_release_manifest.py"
STUB_MARKER = "Purpose: define and store artifacts for this project area."

# files the audit reads outside the package itself
FIXTURE_FILES = [
    "00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md",
    "08_testing/VERIFICATION_REPORT.md",
    "02_firmware/flight_controller/communication/telemetry/telemetry.h",
    "02_firmware/flight_controller/communication/telemetry/telemetry.c",
    "02_firmware/flight_controller/communication/command/cmd_gate.h",
    "02_firmware/flight_controller/configuration/parameters.c",
    "06_communication/ground_station/gs_protocol.py",
]


def build_fixture():
    root = Path(tempfile.mkdtemp(prefix="mfg_audit_"))
    shutil.copytree(ROOT / "12_manufacturing", root / "12_manufacturing")
    for rel in FIXTURE_FILES:
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / rel, dst)
    (root / "01_hardware" / "04_motor_esc").mkdir(parents=True, exist_ok=True)
    (root / "01_hardware" / "01_flight_controller").mkdir(parents=True, exist_ok=True)
    return root


def run(root, args=()):
    proc = subprocess.run([sys.executable, str(AUDIT), "--root", str(root), *args],
                          capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def manifest_of(root):
    return root / "12_manufacturing" / "RELEASE_MANIFEST.json"


def edit_manifest(root, fn):
    path = manifest_of(root)
    data = json.loads(path.read_text(encoding="utf-8"))
    fn(data)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def item(data, item_id):
    return next(i for i in data["items"] if i["id"] == item_id)


def edit_bom(root, fn):
    path = root / "12_manufacturing" / "bom" / "CONTROLLED_BOM.csv"
    lines = path.read_text(encoding="utf-8").splitlines()
    rows = list(csv.DictReader(lines[1:]))
    fn(rows)
    header = lines[1].split(",")
    out = [lines[0], ",".join(header)]
    for row in rows:
        values = []
        for name in header:
            v = row[name.strip()]
            values.append('"%s"' % v if "," in v else v)
        out.append(",".join(values))
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


def append_to(root, rel, text):
    path = root / rel
    path.write_text(path.read_text(encoding="utf-8") + text, encoding="utf-8")


def sub_file(root, rel, old, new):
    path = root / rel
    assert old in path.read_text(encoding="utf-8"), "fixture drift: %s not found in %s" % (old, rel)
    path.write_text(path.read_text(encoding="utf-8").replace(old, new, 1), encoding="utf-8")


CASES = [
    ("present item digest drift",
     lambda r: append_to(r, "12_manufacturing/fabrication/FABRICATION_DATA_SPEC.md", "\nmutated\n"),
     (), "digest mismatch", True),
    ("pinned file degrades to a placeholder stub",
     lambda r: append_to(r, "12_manufacturing/assembly/ASSEMBLY_SEQUENCE.md", STUB_MARKER + "\n"),
     (), "placeholder stub", True),
    ("present item loses its digest",
     lambda r: edit_manifest(r, lambda d: item(d, "MFG-PKG-01").pop("sha256")),
     (), "missing 'sha256'", True),
    ("item with an invented status",
     lambda r: edit_manifest(r, lambda d: item(d, "MFG-PKG-01").update(status="done")),
     (), "has status 'done'", True),
    ("unpinned BOM row with no gate",
     lambda r: edit_bom(r, lambda rows: next(x for x in rows if x["item"] == "MFG-BOM-13").update(gate="")),
     (), "names no gate", True),
    ("BOM gate that resolves to nothing",
     lambda r: edit_bom(r, lambda rows: next(x for x in rows if x["item"] == "MFG-BOM-13").update(gate="ZZ-999")),
     (), "not a declared facility gate nor a requirement", True),
    ("BOM revision out of step with the package",
     lambda r: sub_file(r, "12_manufacturing/bom/CONTROLLED_BOM.csv", "# Revision: MFG-A", "# Revision: MFG-Z"),
     (), "BOM revision", True),
    ("gated directory that is not actually empty",
     lambda r: (r / "12_manufacturing" / "gerbers" / "stray.gbr").write_text("M48\n"),
     (), "was declared empty", True),
    ("facility gate whose blocker no longer holds",
     lambda r: (r / "01_hardware" / "04_motor_esc" / "thrust_measurements.csv").write_text("kv,thrust\n"),
     (), "no longer holds", True),
    ("ground station decoder left behind at an older schema",
     lambda r: sub_file(r, "06_communication/ground_station/gs_protocol.py",
                        "TELEM_SCHEMA_VER = 2", "TELEM_SCHEMA_VER = 3"),
     (), "they must agree", True),
    ("released parameter blob tampered with",
     lambda r: _flip_last_byte(r, "12_manufacturing/parameters/params_defaults.bin"),
     (), "trailing CRC does not cover", True),
    ("gate pointing at a requirement that is already MET",
     lambda r: edit_manifest(r, lambda d: item(d, "MFG-PKG-09").update(gates=["FW-001"])),
     (), "is no longer open", True),
    ("re-baseline without a revision bump",
     None, ("--update-digests", "--revision", "MFG-A"), "must bump the revision", True),
    ("re-baseline with a revision bump succeeds",
     None, ("--update-digests", "--revision", "MFG-B"), "re-baseline to MFG-B", False),
]


def _flip_last_byte(root, rel):
    path = root / rel
    data = bytearray(path.read_bytes())
    data[-1] ^= 0xFF
    path.write_bytes(bytes(data))


def main():
    failures = 0
    print("release manifest audit - negative tests (%d cases + control)" % len(CASES))

    root = build_fixture()
    rc, out = run(root)
    if rc == 0:
        print("  [PASS] control: untouched package audits clean")
    else:
        print("  [FAIL] control: untouched package should audit clean:\n%s" % out)
        failures += 1
    shutil.rmtree(root, ignore_errors=True)

    for name, mutate, args, needle, expect_fail in CASES:
        root = build_fixture()
        if mutate:
            mutate(root)
        rc, out = run(root, args)
        detected = needle in out
        behaved = (rc != 0) == expect_fail and detected
        if behaved:
            print("  [PASS] %s (exit %d, matched %r)" % (name, rc, needle))
        else:
            print("  [FAIL] %s: exit %d (expected %s), match %r = %s"
                  % (name, rc, "non-zero" if expect_fail else "zero", needle, detected))
            print("        " + "\n        ".join(out.strip().splitlines()[-6:]))
            failures += 1
        shutil.rmtree(root, ignore_errors=True)

    # a re-baselined fixture must still audit clean: the bump must not merely
    # silence the digest check
    root = build_fixture()
    append_to(root, "12_manufacturing/pcb_inspection/INSPECTION_CRITERIA.md", "\n")
    rc_upd, _ = run(root, ("--update-digests", "--revision", "MFG-B"))
    rc_chk, out_chk = run(root)
    if rc_upd == 0 and rc_chk == 0:
        print("  [PASS] re-baseline then re-check: clean (revision bumped, digest re-pinned)")
    else:
        print("  [FAIL] re-baseline then re-check: update rc %d, check rc %d\n%s"
              % (rc_upd, rc_chk, out_chk))
        failures += 1
    shutil.rmtree(root, ignore_errors=True)

    total = len(CASES) + 2
    print("SELFTEST: %s (%d/%d cases behaved as specified)"
          % ("PASS" if failures == 0 else "FAIL", total - failures, total))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
