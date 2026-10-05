#!/usr/bin/env python3
"""Requirement-coverage and architecture audit (Phase 23).

Turns three requirements that were previously *claims* into executed checks:

  TEST-004  every baseline requirement has a verification status row
  FW-001    no algorithm file includes a HAL backend header
  FW-004    no dynamic allocation in the control path

Inputs (relative to the repository root):
  00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md   baseline IDs
  08_testing/VERIFICATION_REPORT.md                          status rows
  00_project_control/01_governance/TRACEABILITY_MATRIX.md     traceability rows
  02_firmware/{common,flight_controller}                     algorithm sources
  02_firmware/hal/                                            backends

Usage:  python 08_testing/audit_requirements.py [--verbose] [--json]
Exit:   0 = every check passed, 1 = at least one check failed, 2 = input error.
"""

import json
import os
import re
import sys

REQ_RE = re.compile(r"^\|\s*([A-Z]+-[0-9]+)\s*\|")
# A verification row may carry combined IDs ("CTRL-001/002"), so expand every ID
# token in the leading cell.
CELL_RE = re.compile(r"([A-Z]+-[0-9]+)")
ROW_RE = re.compile(r"^\|\s*([A-Z]+-[0-9]+(?:/[0-9]+)*)\s*\|(.*)$")
STATUSES = ("MET", "PARTIAL", "OPEN")

EXCLUDED_DIRS = {"build", "build-hil", "bootloader", ".git", "__pycache__"}

# Headers that an algorithm file may include: the HAL *contract* is allowed,
# a backend header is not (FW-001).
BACKEND_HEADERS = ("hal_sim.h", "sim_model.h", "hal_hil.h", "hal_stm32.h",
                   "hal_stm32.c")
ALLOC_RE = re.compile(r"\b(malloc|calloc|realloc|free)\s*\(")


def repo_root():
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        os.pardir))


def read(path):
    with open(path, "r", encoding="utf-8") as fh:
        return fh.read()


def baseline_ids(root):
    text = read(os.path.join(root, "00_project_control", "02_requirements",
                             "REQUIREMENTS_DOMAINS.md"))
    ids = []
    for line in text.splitlines():
        m = REQ_RE.match(line.strip())
        if m and m.group(1) not in ids:
            ids.append(m.group(1))
    return ids


def expand(cell):
    """'CTRL-001/002' -> ['CTRL-001', 'CTRL-002']"""
    first = CELL_RE.match(cell)
    if not first:
        return []
    prefix = first.group(1).split("-")[0]
    out = []
    for part in cell.split("/"):
        part = part.strip()
        if not part:
            continue
        out.append(part if "-" in part else "%s-%s" % (prefix, part))
    return [o for o in out if CELL_RE.fullmatch(o)]


def strip_comments(text):
    """Remove C comments so prose like 'allocation-free' is not a match."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def rows_in(path):
    """Requirement IDs that appear in a markdown table row with a status."""
    found = {}
    if not os.path.exists(path):
        return found
    for line in read(path).splitlines():
        m = ROW_RE.match(line.strip())
        if not m:
            continue
        cell, rest = m.group(1), m.group(2)
        if not any(s in rest.upper() for s in STATUSES):
            continue
        for rid in expand(cell):
            found.setdefault(rid, []).append(rest.strip()[:160])
    return found


def source_files(root):
    out = []
    for sub in ("common", "flight_controller"):
        base = os.path.join(root, "02_firmware", sub)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in EXCLUDED_DIRS]
            for f in filenames:
                if f.endswith((".c", ".h")):
                    out.append(os.path.join(dirpath, f))
    return out


def main(argv):
    verbose = "--verbose" in argv
    want_json = "--json" in argv
    root = repo_root()
    failures = []
    report = {}

    # ---- TEST-004: coverage of the baseline -----------------------------
    ids = baseline_ids(root)
    if not ids:
        print("FAIL no baseline requirement IDs parsed")
        return 2
    report["baseline_requirements"] = len(ids)

    vr = rows_in(os.path.join(root, "08_testing", "VERIFICATION_REPORT.md"))
    tm = rows_in(os.path.join(root, "00_project_control", "01_governance",
                              "TRACEABILITY_MATRIX.md"))
    missing = [i for i in ids if i not in vr]
    report["status_rows"] = len(vr)
    report["traceability_rows"] = len(tm)
    report["missing_status"] = missing
    report["missing_traceability"] = [i for i in ids if i not in tm]
    if missing:
        failures.append("requirements without a verification status: %s"
                        % ", ".join(missing))
    if report["missing_traceability"]:
        failures.append("requirements without a traceability row: %s"
                        % ", ".join(report["missing_traceability"]))

    # ---- FW-001: no backend header in algorithm code -------------------
    violations = []
    files = source_files(root)
    for path in files:
        text = strip_comments(read(path))
        for line in text.splitlines():
            stripped = line.strip()
            if not stripped.startswith("#include"):
                continue
            for bad in BACKEND_HEADERS:
                if bad in stripped:
                    violations.append("%s: %s"
                                      % (os.path.relpath(path, root), stripped))
    report["algorithm_files"] = len(files)
    report["backend_include_violations"] = violations
    if violations:
        failures.append("FW-001 backend includes: %s" % "; ".join(violations))

    # ---- FW-004: no dynamic allocation in the control path -------------
    allocs = []
    for path in files + [os.path.join(root, "02_firmware", "hal", "sim", "sim_model.c")]:
        for i, line in enumerate(strip_comments(read(path)).splitlines(), 1):
            if ALLOC_RE.search(line):
                allocs.append("%s:%d: %s" % (os.path.relpath(path, root), i,
                                             line.strip()[:80]))
    report["allocation_sites"] = allocs
    if allocs:
        failures.append("FW-004 dynamic allocation: %s" % "; ".join(allocs))

    report["failures"] = failures
    report["ok"] = not failures

    if want_json:
        print(json.dumps(report, indent=2))
    else:
        print("audit: baseline requirements = %d" % report["baseline_requirements"])
        print("audit: verification status rows = %d, traceability rows = %d"
              % (report["status_rows"], report["traceability_rows"]))
        print("audit: algorithm files scanned = %d (FW-001 violations: %d, "
              "FW-004 allocation sites: %d)"
              % (report["algorithm_files"], len(violations), len(allocs)))
        if verbose and ids:
            print("audit: requirement ids: %s" % " ".join(ids))
        for f in failures:
            print("FAIL %s" % f)
        print("audit: %s" % ("PASS" if report["ok"] else "FAIL"))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))