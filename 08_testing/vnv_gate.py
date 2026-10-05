#!/usr/bin/env python3
"""Formal V&V review gate (Phase 27).

Reviews the requirement-to-design-to-implementation-to-test-to-evidence chain for
every baseline requirement, and turns the phase's acceptance criterion into an
executed check:

    all critical requirements are either VERIFIED or EXPLICITLY BLOCKED.

For each of the 83 baseline IDs (`00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md`):

  * the authoritative status comes from the verification report row. The *last*
    status token in the row wins, because the compound rows put the caveat last
    ("**MET (review)**; board unbuilt -> **OPEN (H)**" is a blocked item, not a
    verified one). Outcomes: verified / partial / blocked(H) / open.
  * every reference in backticks that looks like a file path must resolve to a
    file that exists. An evidence reference that points at nothing is how a
    review reads green while the artifact is gone.
  * a CRITICAL requirement (the loss-of-control chain, listed below) that is not
    verified must carry a reason in writing (an em-dash explanation in the status
    row). "Not done" without a named blocker is a failed review item, not a
    residual risk.

Non-critical PARTIAL/OPEN rows are *reported* as the residual-risk register and do
not fail the gate; a review that hides them would defeat the phase.

    python 08_testing/vnv_gate.py               # review, `VNV: PASS` / `VNV: FAIL`
    python 08_testing/vnv_gate.py --selftest    # 7 fixture cases run through this CLI
    python 08_testing/vnv_gate.py --root DIR    # review another tree (fixtures)

This gate is one half of the phase: the narrative review with the residual-risk
table, non-claims and the certification refusal is `08_testing/VNV_REVIEW.md`.
Neither document claims certification for this vehicle.
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# The loss-of-control chain: if one of these fails, the vehicle can be lost or a
# hazard can go unprotected. Failure is not always avoidable here (no board), but
# it must be *recorded*, which is what the reason requirement enforces.
REVIEW_DOC = "08_testing/VNV_REVIEW.md"
CRITICAL_IDS = (
    "SYS-001", "SYS-002", "SYS-003", "SYS-004",        # rates, latency, RC loss, fallback
    "FW-002", "FW-003",                                  # scheduler/watchdog, bus-fault containment
    "EST-001",                                           # attitude estimate
    "CTRL-001", "CTRL-002", "CTRL-003", "CTRL-005",      # cascade, bounds, mixer, disarm
    "NAV-004",                                           # geofence -> RTL
    "COM-004", "GS-003",                                 # authority boundary, action on the wire
)
CRITICAL_PREFIXES = ("SAF-",)                            # failsafe, battery, avoidance bounds

REQ_RE = re.compile(r"^\|\s*([A-Z]+-[0-9]+)\s*\|")
ROW_RE = re.compile(r"^\|\s*([A-Z]+-[0-9]+(?:/[0-9]+)*)\s*\|(.*)$")
# status tokens; the LAST one in a row is the outcome (caveats come last)
STATUS_RE = re.compile(r"\*\*(MET|PARTIAL|OPEN)([^*|]*)\*\*", re.IGNORECASE)
PATH_RE = re.compile(r"`([^`]+)`")
PATH_SUFFIXES = (".c", ".h", ".py", ".md", ".json", ".csv", ".txt", ".sh", ".bin", ".yaml")

STATUS_WORDS = ("MET", "PARTIAL", "OPEN")


def classify_cells(cells):
    """(outcome, status_cell) for a row. The LAST status token in the row is the
    outcome (compound rows put the caveat last); outcome is one of
    verified/partial/blocked/open, or None when no status token exists."""
    last = None
    for idx, cell in enumerate(cells):
        for m in STATUS_RE.finditer(cell):
            last = (idx, m)
    if last is None:
        return None, ""
    idx, m = last
    word = m.group(1).upper()
    tail = m.group(2)
    if word == "MET":
        outcome = "verified"
    elif word == "PARTIAL":
        outcome = "partial"
    elif "H" in tail.upper():
        outcome = "blocked"
    else:
        outcome = "open"
    return outcome, cells[idx]


def has_reason(cell):
    """A recorded blocker must be written down, not implied: either an explicit
    em-dash explanation, or at least two words of explanation besides the verdict
    tokens themselves (compound rows name the blocker before the last token, e.g.
    'MET (SIM); bus-level faults OPEN (H)')."""
    if "\u2014" in cell:
        return True
    words = re.sub(r"[^A-Za-z0-9_]+", " ", STATUS_RE.sub("", cell)).split()
    return len(words) >= 2


def is_critical(rid):
    return rid in CRITICAL_IDS or rid.startswith(CRITICAL_PREFIXES)


def expand(cell):
    """'CTRL-001/002' -> ['CTRL-001', 'CTRL-002'] (same rule as the audit)."""
    first = re.match(r"([A-Z]+)-", cell)
    if not first:
        return []
    prefix = first.group(1)
    out = []
    for part in cell.split("/"):
        part = part.strip()
        if not part:
            continue
        out.append(part if "-" in part else "%s-%s" % (prefix, part))
    return [o for o in out if re.fullmatch(r"[A-Z]+-[0-9]+", o)]


def read(path):
    return Path(path).read_text(encoding="utf-8", errors="replace")


def row_cells(path, wanted=None):
    """ID -> the row's remaining cells (after the ID cell). Combined ID cells
    ("CTRL-001/002") are expanded so every ID finds its row."""
    found = {}
    for line in read(path).splitlines():
        m = ROW_RE.match(line.strip())
        if not m:
            continue
        cell, rest = m.group(1), m.group(2)
        cells = [c.strip() for c in rest.split("|")]
        for rid in expand(cell):
            if wanted is None or rid in wanted:
                found.setdefault(rid, cells)
    return found


def baseline_ids(root):
    text = read(Path(root) / "00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md")
    ids = []
    for line in text.splitlines():
        m = REQ_RE.match(line.strip())
        if m and m.group(1) not in ids:
            ids.append(m.group(1))
    return ids


def path_refs(text, top_dirs):
    """Backticked references that are plausibly paths. Excludes code (contains
    braces/parentheses/commas) and slash-separated code names such as
    `malloc/calloc/realloc/free`, which would otherwise read as a missing file."""
    out = []
    for token in PATH_RE.findall(text):
        token = token.strip().strip(".,;:")
        if any(c in token for c in "{}()+=*,"):
            continue
        if not ("/" in token or token.endswith(PATH_SUFFIXES)):
            continue
        if "/" in token and not (
                token.endswith(PATH_SUFFIXES) or token.endswith("/")
                or any("." in part for part in token.split("/"))
                or token.split("/")[0] in top_dirs):
            continue
        out.append(token)
    return out


def resolve(root, ref, names):
    """A reference resolves if it exists as given, or as a basename somewhere in
    the tree (the documents sometimes name `run_regression.sh` without its path)."""
    candidate = Path(root) / ref
    if candidate.exists():
        return True
    return Path(ref).name in names


def run(root):
    ids = baseline_ids(root)
    if not ids:
        print("FAIL no baseline requirement IDs parsed")
        return 2, {}
    vr = row_cells(Path(root) / "08_testing/VERIFICATION_REPORT.md", set(ids))
    tm = row_cells(Path(root) / "00_project_control/01_governance/TRACEABILITY_MATRIX.md", set(ids))

    names = set()
    for path in Path(root).rglob("*"):
        if any(part in ("build", "build-hil", ".git", "__pycache__") for part in path.parts):
            continue
        if path.is_file():
            names.add(path.name)
    top_dirs = {p.name for p in Path(root).iterdir() if p.is_dir()}

    outcomes = {"verified": [], "partial": [], "blocked": [], "open": []}
    failures, residuals, broken = [], [], []
    critical_total = 0
    evidence_checked = 0

    for rid in ids:
        cells = vr.get(rid)
        if cells is None:
            failures.append("%s: no verification status row" % rid)
            continue
        outcome, status_cell = classify_cells(cells)
        if outcome is None:
            failures.append("%s: no status token in its verification row" % rid)
            continue
        outcomes[outcome].append(rid)

        refs = (path_refs(" ".join(cells), top_dirs)
                + path_refs(" ".join(tm.get(rid, [])), top_dirs))
        resolved = [r for r in refs if resolve(root, r, names)]
        lost = [r for r in refs if r not in resolved]
        evidence_checked += len(refs)
        if lost:
            broken.append("%s: %s" % (rid, ", ".join(lost[:3])))

        if is_critical(rid):
            critical_total += 1
            if outcome == "verified":
                if refs and not resolved:
                    failures.append("%s (critical): no resolvable evidence reference" % rid)
            elif not has_reason(status_cell):
                failures.append("%s (critical, %s): no reason recorded in %r"
                                % (rid, outcome, status_cell[:60]))
            if lost:
                failures.append("%s (critical): broken evidence reference(s): %s"
                                % (rid, ", ".join(lost[:3])))
        elif outcome in ("partial", "open", "blocked"):
            residuals.append("%s: %s" % (rid, outcome))

    # The review document is the narrative half; like every other count in this
    # repository, the numbers it quotes must equal the gate's. Only checked when
    # the document exists (fixtures exercise the classification, not the prose).
    review = Path(root) / REVIEW_DOC
    if review.exists():
        unverified = len(outcomes["partial"]) + len(outcomes["blocked"]) + len(outcomes["open"])
        required = ["%d baseline" % len(ids), "%d critical" % critical_total,
                    "%d verified" % len(outcomes["verified"]), "%d unverified" % unverified]
        absent = [s for s in required if s not in read(review)]
        if absent:
            failures.append("%s does not quote the current gate result (missing %s)"
                            % (REVIEW_DOC, ", ".join(absent)))

    print("VNV review: %d baseline requirement(s), %d critical" % (len(ids), critical_total))
    for key in ("verified", "partial", "blocked", "open"):
        print("  %-8s %d" % (key, len(outcomes[key])))
    print("  unverified %d (partial %d, blocked %d, open %d)"
          % (len(outcomes["partial"]) + len(outcomes["blocked"]) + len(outcomes["open"]),
             len(outcomes["partial"]), len(outcomes["blocked"]), len(outcomes["open"])))
    print("  evidence references resolved: %d checked, %d broken"
          % (evidence_checked, len(broken)))
    if residuals:
        print("  residual risks (non-critical, recorded): %s" % ", ".join(residuals))
    for line in broken:
        print("  broken evidence reference: %s" % line)

    critical_bad = [f for f in failures if "(critical" in f]
    if failures:
        for line in failures:
            print("FAIL %s" % line)
        print("VNV: FAIL (%d review item(s), %d critical)" % (len(failures), len(critical_bad)))
        return 1, outcomes
    print("VNV: PASS (every critical requirement verified or explicitly blocked)")
    return 0, outcomes


def fixture(root, rows, evidence=True, broken_ref=False):
    """Tiny but valid review tree: one row per requirement in each document."""
    (root / "00_project_control/02_requirements").mkdir(parents=True, exist_ok=True)
    (root / "00_project_control/01_governance").mkdir(parents=True, exist_ok=True)
    (root / "08_testing").mkdir(parents=True, exist_ok=True)
    req = ["# Requirements", "", "| ID | Requirement | Acceptance | Ver |", "|---|---|---|---|"]
    for rid in rows:
        req.append("| %s | shall do %s | verified | T |" % (rid, rid))
    (root / "00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md").write_text(
        "\n".join(req) + "\n", encoding="utf-8")
    vr = ["# Verification", "", "| ID | method | evidence | status |", "|---|---|---|---|"]
    tm = ["# Traceability", "", "| ID | short | design | impl | verification | evidence | status |"]
    for rid, status in rows.items():
        ref = "`ev/missing.c`" if broken_ref and is_critical(rid) else "`ev/report.md`"
        vr.append("| %s | method | evidence %s | **%s** |" % (rid, ref, status))
        tm.append("| %s | short | design | impl | test | %s | %s |" % (rid, ref, status))
    (root / "08_testing/VERIFICATION_REPORT.md").write_text("\n".join(vr) + "\n", encoding="utf-8")
    (root / "00_project_control/01_governance/TRACEABILITY_MATRIX.md").write_text(
        "\n".join(tm) + "\n", encoding="utf-8")
    if evidence:
        (root / "ev").mkdir(exist_ok=True)
        (root / "ev/report.md").write_text("evidence\n", encoding="utf-8")


def selftest():
    cases = [
        ("control: critical MET with evidence", {"SYS-001": "MET (SIM) — verified in the suite"}, False, 0, "VNV: PASS"),
        ("critical PARTIAL with a written reason", {"SYS-001": "PARTIAL \u2014 deviation named"}, False, 0, "VNV: PASS"),
        ("critical OPEN (H) with a named blocker", {"SYS-001": "OPEN (H) \u2014 no board exists"}, False, 0, "VNV: PASS"),
        ("critical PARTIAL with no reason", {"SYS-001": "PARTIAL"}, False, 1, "VNV: FAIL"),
        ("missing verification row", {"SYS-001": "MET (SIM) \u2014 ok", "SYS-002": None}, False, 1, "VNV: FAIL"),
        ("critical verified but evidence path resolves to nothing",
         {"SYS-001": "MET (SIM) \u2014 ok"}, True, 1, "VNV: FAIL"),
        ("non-critical OPEN is a residual risk, not a failure",
         {"TEST-001": "OPEN \u2014 pending"}, False, 0, "VNV: PASS"),
    ]
    good = 0
    for name, rows, broken_ref, want_rc, want_text in cases:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            present = {k: v for k, v in rows.items() if v is not None}
            fixture(root, present, broken_ref=broken_ref)
            if any(v is None for v in rows.values()):
                # a listed requirement with no row must fail; add it to the baseline
                extra = [k for k, v in rows.items() if v is None][0]
                with open(root / "00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md",
                          "a", encoding="utf-8") as fh:
                    fh.write("| %s | shall exist | \u2014 | T |\n" % extra)
            proc = subprocess.run([sys.executable, str(Path(__file__).resolve()),
                                   "--root", str(root)],
                                  capture_output=True, text=True)
            ok = proc.returncode == want_rc and want_text in proc.stdout
            print("%s %s" % ("PASS" if ok else "FAIL", name))
            if ok:
                good += 1
            else:
                print("     rc=%d want=%d output=%r" % (proc.returncode, want_rc,
                                                        proc.stdout[-200:]))
    print("VNV selftest: %d/%d cases behaved as specified" % (good, len(cases)))
    return 0 if good == len(cases) else 1


def main():
    ap = argparse.ArgumentParser(description="formal V&V review gate")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[1]),
                    help="repository root to review (default: this repository)")
    ap.add_argument("--selftest", action="store_true",
                    help="run the fixture cases through this same CLI")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    rc, _ = run(Path(args.root))
    return rc


if __name__ == "__main__":
    sys.exit(main())
