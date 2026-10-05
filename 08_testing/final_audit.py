#!/usr/bin/env python3
"""Final A-Z audit: unresolved-work markers are a register, not a vibe (Phase 29).

The old failure mode: `TODO`/`TBD` strings scattered through the repository,
some of them stale leftovers of early scaffolding, some of them honest
statements of what hardware would still have to answer. Nobody can tell which is
which without reading every file, which is exactly the ambiguity Phase 26 removed
for document status and this tool removes for unresolved work.

It scans every project artifact (markdown, C/H, Python, shell; `15_prompts/` is
process material, not a project artifact) for the marker words and requires each
(path, marker) pair to be listed in the findings block of `08_testing/FINAL_AUDIT.md`
with its occurrence count and a disposition:

  * `INTENTIONAL` - the word is prose (a rule explaining what a "TBD" row means).
  * `RESIDUAL`    - a real open item; the disposition must name its gate/owner.
  * `FIXED-IN-PHASE-29` - allowed only with count `0` (the audit recorded a fix).

Two-way, like the documentation index: a scan hit missing from the table fails
(an undocumented TODO appeared), and a table entry whose count no longer matches
fails (a stale fix or a changed file). Excluded from the scan: this script (it
contains the marker words) and `FINAL_AUDIT.md` itself (it is the register).

    python 08_testing/final_audit.py            # `FINAL AUDIT: PASS`
    python 08_testing/final_audit.py --selftest # 6 fixture cases through this CLI
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FINDINGS_REL = "08_testing/FINAL_AUDIT.md"
SELF_REL = "08_testing/final_audit.py"
BEGIN = "<!-- final-audit:findings -->"
END = "<!-- /final-audit:findings -->"
MARKERS = ("TODO", "FIXME", "XXX", "HACK", "TBD")
MARKER_RE = re.compile(r"\b(%s)\b" % "|".join(MARKERS))
SUFFIXES = (".md", ".c", ".h", ".py", ".sh")
SKIP_DIRS = ("build", "build-hil", ".git", "__pycache__")
SKIP_PREFIX = "15_prompts/"
DISPOSITIONS = ("INTENTIONAL", "RESIDUAL", "FIXED-IN-PHASE-29")
ROW_RE = re.compile(r"^\|\s*`([^`]+)`\s*\|\s*([A-Z]+)\s*\|\s*([0-9]+)\s*\|\s*([A-Z0-9-]+)(.*)\|\s*$")


def scan(root):
    """{(relpath, marker): count} over every project artifact."""
    found = {}
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in SUFFIXES:
            continue
        rel = path.relative_to(root).as_posix()
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        if rel.startswith(SKIP_PREFIX) or rel in (FINDINGS_REL, SELF_REL):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for marker in MARKERS:
            count = len(re.findall(r"\b%s\b" % marker, text))
            if count:
                found[(rel, marker)] = count
    return found


def parse_findings(text):
    if BEGIN not in text or END not in text:
        raise LookupError("findings markers %s / %s not found in %s" % (BEGIN, END, FINDINGS_REL))
    block = text.split(BEGIN, 1)[1].split(END, 1)[0]
    rows = {}
    for line in block.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        m = ROW_RE.match(line)
        if not m:
            continue
        rel, marker, count, disposition, rest = (m.group(1), m.group(2),
                                                 int(m.group(3)), m.group(4),
                                                 m.group(5).strip())
        if marker not in MARKERS:
            raise ValueError("unknown marker %r in findings row" % marker)
        if disposition not in DISPOSITIONS:
            raise ValueError("unknown disposition %r in findings row" % disposition)
        # any words after the disposition name the gate/owner
        detail = rest.lstrip("|").strip()
        if rel in (FINDINGS_REL, SELF_REL):
            raise ValueError("findings row must not point at the register itself: %s" % rel)
        rows[(rel, marker)] = (count, disposition, detail)
    return rows


def run(root, scan_only=False):
    problems = []
    if scan_only:
        found = scan(root)
        for key, count in sorted(found.items()):
            print("FOUND %s %s x%d" % (key[0], key[1], count))
        print("final audit scan: %d marker occurrence(s) in %d file(s)"
              % (sum(found.values()), len({k[0] for k in found})))
        return 0
    try:
        rows = parse_findings((root / FINDINGS_REL).read_text(encoding="utf-8"))
    except (LookupError, ValueError) as exc:
        print("FAIL %s" % exc)
        print("FINAL AUDIT: FAIL (1 problem(s))")
        return 1
    found = scan(root)

    for key, count in sorted(found.items()):
        if key not in rows:
            problems.append("%s:%s x%d is not in the findings register" % (key[0], key[1], count))
            continue
        stored, disposition, detail = rows[key]
        if stored != count:
            problems.append("%s:%s register says %d, scan found %d"
                            % (key[0], key[1], stored, count))
        if disposition == "RESIDUAL" and len(detail.split()) < 2:
            problems.append("%s:%s RESIDUAL with no gate/owner named" % key)
        if disposition == "FIXED-IN-PHASE-29" and count != 0:
            problems.append("%s:%s marked fixed but %d occurrence(s) remain"
                            % (key[0], key[1], count))
    for key, (count, disposition, detail) in sorted(rows.items()):
        if key not in found:
            if disposition == "FIXED-IN-PHASE-29" and count == 0:
                continue                      # the recorded fix: nothing left to find
            problems.append("%s:%s is registered (%d) but the scan finds none"
                            % (key[0], key[1], count))

    for line in problems:
        print("FAIL %s" % line)
    if problems:
        print("FINAL AUDIT: FAIL (%d problem(s))" % len(problems))
        return 1
    print("final audit: %d marker occurrence(s) in %d file(s), every one dispositioned"
          % (sum(found.values()), len({k[0] for k in found})))
    print("final audit: register holds %d entr(ies) (incl. recorded fixes)" % len(rows))
    print("FINAL AUDIT: PASS")
    return 0


# ---------------------------------------------------------------------------
# Negative tests: fixture trees run through this same CLI (DEC-023).
# ---------------------------------------------------------------------------
FIXTURE_FINDINGS = """# Fixture audit

{BEGIN}
| File | Marker | Count | Disposition | Gate / owner |
|---|---|---|---|---|
| `src/app.c` | TODO | {count} | {disposition} | {detail} |
{END}
"""


def fixture(root, hit=True, count=1, disposition="RESIDUAL",
            detail="wiring deferred to phase 6", extra_hit=False):
    (root / "08_testing").mkdir(parents=True, exist_ok=True)
    (root / "src").mkdir(exist_ok=True)
    if hit:
        (root / "src/app.c").write_text("/* TODO: wire it */\n", encoding="utf-8")
    else:
        (root / "src/app.c").write_text("/* wired */\n", encoding="utf-8")
    if extra_hit:
        (root / "src/other.c").write_text("/* TODO: another */\n", encoding="utf-8")
    (root / FINDINGS_REL).write_text(
        FIXTURE_FINDINGS.format(BEGIN=BEGIN, END=END, count=count,
                                disposition=disposition, detail=detail),
        encoding="utf-8")


def case(root):
    proc = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--root", str(root)],
                          capture_output=True, text=True)
    return proc.returncode, proc.stdout


def selftest():
    ok = []

    def run_case(name, want_rc, want_text, **kw):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            fixture(root, **kw)
            rc, out = case(root)
            good = rc == want_rc and want_text in out
            print("%s %s" % ("PASS" if good else "FAIL", name))
            if not good:
                print("     rc=%d want=%d out=%r" % (rc, want_rc, out[-200:]))
            ok.append(good)

    run_case("control: registered marker audits clean", 0, "FINAL AUDIT: PASS")
    run_case("unregistered marker fails", 1, "not in the findings register", extra_hit=True)
    run_case("count mismatch fails", 1, "register says 2, scan found 1", count=2)
    run_case("RESIDUAL without a gate fails", 1, "no gate/owner named", detail="")
    run_case("recorded fix with zero occurrences passes", 0, "FINAL AUDIT: PASS",
             hit=False, count=0, disposition="FIXED-IN-PHASE-29", detail="fixed in the audit")
    run_case("stale entry (registered, nothing on disk) fails", 1,
             "but the scan finds none", hit=False, count=1)
    print("final audit selftest: %d/%d cases behaved as specified" % (sum(ok), len(ok)))
    return 0 if all(ok) else 1


def main():
    ap = argparse.ArgumentParser(description="final A-Z audit: marker register")
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--scan", action="store_true", help="print current hits only")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    return run(Path(args.root), scan_only=args.scan)


if __name__ == "__main__":
    sys.exit(main())
