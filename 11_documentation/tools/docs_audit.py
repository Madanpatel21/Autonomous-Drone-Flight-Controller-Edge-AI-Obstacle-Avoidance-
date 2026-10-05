#!/usr/bin/env python3
"""Audit the documentation layer (Phase 26, regression step 10).

Five questions, each one that was previously answerable only by reading the
whole repository:

1. index      DOCUMENTATION_INDEX.md declares a status for each authoritative
              document. Does the file exist, and is the declared status true in
              *both* directions? A document declared `content` must not carry a
              scaffold/task-stub banner, and one declared `scaffold`/`task stub`
              must actually carry the matching banner.
2. links      every relative link in the documentation entry points resolves to
              a file that exists. "The workflow is reproducible" is a broken
              link away from being false for a new engineer.
3. scan       no markdown file anywhere is an *unmarked* placeholder or
              instruction-only stub (same detectors as mark_scaffolds.py, so the
              two tools can never disagree). A banner that has content appended
              underneath it is also caught, since that is how a scaffold quietly
              turns back into a claim.
4. derived    PROJECT_FACTS.json.derived is re-read from the source of truth for
              each value (firmware headers, released parameter blob, release
              manifest). Numbers quoted in prose have one home; this keeps it
              honest without a human re-checking a table.
5. observed   counts that only a run can know (suite size, executed steps, GS
              test count, ...) are supplied by the regression runner and must
              match the file. A stale number is then a red build, not a stale
              document.

    python 11_documentation/tools/docs_audit.py                    # audit
    python 11_documentation/tools/docs_audit.py --observed k=v ... # what the runner does
    python 11_documentation/tools/docs_audit.py --update-facts \
        --observed executed_steps=27 ...                           # deliberate re-baseline

Exit status is 0 only when every check passes; the last line is `DOCS: PASS` or
`DOCS: FAIL (n problem(s))`. Structural problems (1-3) are never masked by
`--update-facts`: only the facts file can be re-baselined by it.
"""
import argparse
import datetime
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mark_scaffolds import (BANNER_MARKER, TASK_BANNER_MARKER, IMPERATIVES,
                            is_placeholder, is_task_stub)

ROOT = Path(__file__).resolve().parents[2]
INDEX_REL = "11_documentation/DOCUMENTATION_INDEX.md"
INDEX = ROOT / INDEX_REL
FACTS_REL = "11_documentation/PROJECT_FACTS.json"
FACTS = ROOT / FACTS_REL
WALKTHROUGH_REL = "11_documentation/NEW_ENGINEER_WALKTHROUGH.md"
# Documents whose counted prose is checked against the facts file, in addition
# to the index/link/marker checks. Both are pinned items of the release.
QUOTE_DOCS = [WALKTHROUGH_REL, "13_release/RELEASE_NOTES.md"]
# The documents a stranger is told to read first; a dead link here is the most
# expensive one in the repository.
ENTRY_DOCS = [
    "11_documentation/README.md",
    INDEX_REL,
    WALKTHROUGH_REL,
    "11_documentation/TROUBLESHOOTING.md",
]
BEGIN = "<!-- docs-audit:authoritative -->"
END = "<!-- /docs-audit:authoritative -->"
STATUSES = ("content", "scaffold", "task stub")
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")

# Prose in the walkthrough that quotes a fact. Each pattern's capture groups map
# positionally to PROJECT_FACTS.json key names. A pattern that no longer matches
# is not an error (the sentence may have been reworded), but every match that
# does appear must agree with the facts file.
QUOTE_PATTERNS = [
    (r"(\d+)\s*/\s*(\d+)\s+passed", ("unit_checks", "unit_checks_total")),
    (r"(\d+)\s+executed steps", ("executed_steps",)),
    (r"(\d+)\s+H-gated SKIPs", ("gated_skips",)),
    (r"Ran (\d+) tests", ("gs_tests",)),
    (r"(\d+) baseline requirements", ("baseline_requirements",)),
    (r"(\d+) algorithm files", ("algorithm_files",)),
    (r"(\d+)\s+checks passed", ("package_audit_checks",)),
    (r"(\d+)/(\d+) cases behaved", ("package_audit_negative_cases",
                                    "package_audit_negative_cases")),
]

PROBLEMS = []


def fail(group, message):
    PROBLEMS.append((group, message))


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def grep_int(rel, pattern):
    m = re.search(pattern, read(ROOT / rel))
    if not m:
        raise LookupError("no match for %r in %s" % (pattern, rel))
    return int(m.group(1))


def has_scaffold_banner(text):
    """True only for the banner itself, not a document that quotes it."""
    return any(ln.strip().startswith("> " + BANNER_MARKER)
               for ln in text.splitlines())


def has_task_banner(text):
    return any(ln.strip().startswith("> " + TASK_BANNER_MARKER)
               for ln in text.splitlines())


def banner_extras(text, kind):
    """Describe content sitting under a banner, or None if the banner is intact.

    A scaffold that has grown content, or a task stub whose instructions were
    replaced by results, must lose its banner and be declared in the index -
    otherwise the marker stops meaning anything.
    """
    marker = BANNER_MARKER if kind == "scaffold" else TASK_BANNER_MARKER
    rest, in_banner = [], False
    for ln in text.splitlines():
        s = ln.strip()
        if not in_banner:
            if s.startswith("> " + marker):
                in_banner = True
            continue
        if s == "" or s.startswith(">"):
            continue
        in_banner = False
        rest.append(s)
    if not rest:
        return None
    if kind == "scaffold":
        return "text: %r" % rest[0][:60]
    if any("```" in ln or "](" in ln or "|" in ln for ln in rest):
        return "a table, code block or link"
    if not all(ln.startswith(IMPERATIVES) or ln.endswith("here.") for ln in rest):
        return "a non-instruction line: %r" % rest[0][:60]
    return None


def derive():
    """Re-read every derived fact from the artifact that owns it."""
    manifest = json.loads(read(ROOT / "12_manufacturing/RELEASE_MANIFEST.json"))
    items = manifest.get("items", [])
    params = json.loads(read(ROOT / "12_manufacturing/parameters/params_defaults.json"))
    return {
        "package_revision": manifest["package"]["revision"],
        "package_items": len(items),
        "package_items_present": sum(1 for i in items if i.get("status") == "present"),
        "package_items_gated": sum(1 for i in items if i.get("status") == "gated"),
        "telemetry_schema_version": grep_int(
            "02_firmware/flight_controller/communication/telemetry/telemetry.h",
            r"TELEM_SCHEMA_VER\s+(\d+)u"),
        "telemetry_payload_bytes": grep_int(
            "02_firmware/flight_controller/communication/telemetry/telemetry.c",
            r"TELEM_STATUS_PAYLOAD\s+(\d+)u"),
        "param_blob_version": params["fields"]["version"],
        "param_blob_crc": params["crc"],
        "cmd_gate_version": grep_int(
            "02_firmware/flight_controller/communication/command/cmd_gate.h",
            r"CMD_VERSION\s+(\d+)u"),
    }


def consistency(fresh):
    """The decoder must not fall behind the encoder (COM-003, Phase 24)."""
    gs = read(ROOT / "06_communication/ground_station/gs_protocol.py")
    m = re.search(r"TELEM_SCHEMA_VER\s*=\s*(\d+)", gs)
    if not m or int(m.group(1)) != fresh["telemetry_schema_version"]:
        fail("derived", "GS decoder telemetry schema %s != FC %s"
             % (m.group(1) if m else "?", fresh["telemetry_schema_version"]))
    m = re.search(r"TELEM_STATUS_PAYLOAD\s*=\s*(\d+)", gs)
    if not m or int(m.group(1)) != fresh["telemetry_payload_bytes"]:
        fail("derived", "GS decoder payload %s != FC %s"
             % (m.group(1) if m else "?", fresh["telemetry_payload_bytes"]))


def parse_index(text):
    if BEGIN not in text or END not in text:
        fail("index", "authoritative-table markers %s / %s not found" % (BEGIN, END))
        return []
    block = text.split(BEGIN, 1)[1].split(END, 1)[0]
    rows = []
    for ln in block.splitlines():
        ln = ln.strip()
        if not ln.startswith("|"):
            continue
        cells = [c.strip() for c in ln.strip("|").split("|")]
        if len(cells) != 3 or cells[0].lower() == "document":
            continue
        if set(cells[0]) <= set("-: "):
            continue
        m = re.match(r"^`([^`]+)`$", cells[0])
        if not m:
            fail("index", "row is not a backticked path: %s" % ln)
            continue
        rows.append((m.group(1), cells[2]))
    return rows


def check_index(rows):
    seen = set()
    counts = {}
    for rel, status in rows:
        if rel in seen:
            fail("index", "%s: listed twice" % rel)
        seen.add(rel)
        counts[status] = counts.get(status, 0) + 1
        path = ROOT / rel
        if not path.is_file():
            fail("index", "%s: declared %r but the file does not exist" % (rel, status))
            continue
        text = read(path)
        scaf, task = has_scaffold_banner(text), has_task_banner(text)
        if status == "content" and (scaf or task):
            fail("index", "%s: declared content but carries a %s banner"
                 % (rel, "scaffold" if scaf else "task-stub"))
        elif status == "scaffold" and not scaf:
            fail("index", "%s: declared scaffold but no scaffold banner on disk" % rel)
        elif status == "task stub" and not task:
            fail("index", "%s: declared task stub but no task-stub banner on disk" % rel)
        elif status not in STATUSES:
            fail("index", "%s: unknown status %r" % (rel, status))
    return counts


def check_links():
    checked = 0
    for rel in ENTRY_DOCS:
        path = ROOT / rel
        if not path.is_file():
            fail("links", "%s: entry document missing" % rel)
            continue
        for target in LINK_RE.findall(read(path)):
            target = target.strip()
            if target.startswith(("http://", "https://", "mailto:")):
                continue
            target = target.split("#", 1)[0]
            if not target:
                continue
            checked += 1
            if not (path.parent / target).resolve().exists():
                fail("links", "%s: broken link -> %s" % (rel, target))
    return checked


def scan_repository():
    scaffolds = tasks = files = 0
    for path in sorted(ROOT.rglob("*.md")):
        if any(part in ("build", "build-hil", ".git") for part in path.parts):
            continue
        files += 1
        rel = str(path.relative_to(ROOT)).replace("\\", "/")
        text = read(path)
        if has_scaffold_banner(text):
            scaffolds += 1
            extra = banner_extras(text, "scaffold")
            if extra:
                fail("scan", "%s: scaffold banner with content underneath (%s)" % (rel, extra))
        elif has_task_banner(text):
            tasks += 1
            extra = banner_extras(text, "task")
            if extra:
                fail("scan", "%s: task-stub banner but the body now contains %s" % (rel, extra))
        elif is_placeholder(text):
            fail("scan", "%s: unmarked placeholder (run mark_scaffolds.py --write)" % rel)
        elif is_task_stub(text):
            fail("scan", "%s: unmarked instruction-only stub (run mark_scaffolds.py --write)" % rel)
    return files, scaffolds, tasks


def compare(stored, fresh, group):
    """Count compared keys; fail on any disagreement. Keys not measured now are
    left alone (their check did not run this time)."""
    compared = 0
    for key in sorted(set(stored) | set(fresh)):
        if key not in stored:
            fail(group, "measured %s=%r is not in %s (add it with --update-facts)"
                 % (key, fresh[key], FACTS_REL))
            continue
        if key not in fresh:
            continue
        compared += 1
        if stored[key] != fresh[key]:
            fail(group, "%s: file says %r, this run measured %r"
                 % (key, stored[key], fresh[key]))
    return compared


def check_quotes(facts):
    patterns = 0
    values = 0
    for rel in QUOTE_DOCS:
        text = read(ROOT / rel)
        for pattern, keys in QUOTE_PATTERNS:
            found = re.findall(pattern, text)
            if not found:
                continue
            patterns += 1
            for match in found:
                groups = match if isinstance(match, tuple) else (match,)
                for key, raw in zip(keys, groups):
                    if key not in facts:
                        continue
                    values += 1
                    if str(facts[key]) != str(int(raw)):
                        fail("quotes", "%s: says %s, PROJECT_FACTS.json says %r"
                             % (rel, raw, facts[key]))
    return patterns, values


def parse_observed(pairs):
    observed = {}
    for pair in pairs:
        if "=" not in pair:
            fail("observed", "--observed %r is not KEY=VALUE" % pair)
            continue
        key, _, raw = pair.partition("=")
        try:
            value = int(raw)
        except ValueError:
            try:
                value = float(raw)
            except ValueError:
                value = raw
        observed[key.strip()] = value
    return observed


def main():
    ap = argparse.ArgumentParser(description="audit documentation claims and counts")
    ap.add_argument("--observed", action="extend", nargs="+", default=[],
                    metavar="KEY=VALUE",
                    help="a count only this run can observe (one or more)")
    ap.add_argument("--update-facts", action="store_true",
                    help="write measured values into PROJECT_FACTS.json (deliberate)")
    args = ap.parse_args()

    facts_text = read(FACTS)
    facts = json.loads(facts_text)
    stored_derived = facts.get("derived", {})
    stored_observed = facts.get("observed", {})

    # ---- 1-3. structure ------------------------------------------------------
    index_text = read(INDEX) if INDEX.is_file() else ""
    if not index_text:
        fail("index", "%s missing" % INDEX_REL)
    counts = check_index(parse_index(index_text))
    checked_links = check_links()
    files, scaffolds, tasks = scan_repository()

    # ---- 4-5. numbers --------------------------------------------------------
    supplied = parse_observed(args.observed)
    try:
        fresh_derived = derive()
        consistency(fresh_derived)
    except (LookupError, KeyError, json.JSONDecodeError) as exc:
        fail("derived", "cannot re-derive: %s" % exc)
        fresh_derived = {}

    changes = []
    if args.update_facts:
        merged = dict(facts)
        for section, fresh in (("derived", fresh_derived), ("observed", supplied)):
            base = dict(merged.get(section, {}))
            for key, value in fresh.items():
                if base.get(key) != value:
                    changes.append("%s.%s: %r -> %r"
                                   % (section, key, base.get(key), value))
                base[key] = value
            merged[section] = base
        merged["updated"] = datetime.date.today().isoformat()
        new_text = json.dumps(merged, indent=2) + "\n"
        if new_text != facts_text:
            FACTS.write_text(new_text, encoding="utf-8")
        facts = merged
        stored_derived = facts["derived"]
        stored_observed = facts["observed"]
        # the updated values are now the baseline, so they cannot also fail
        for key in list(supplied):
            supplied[key] = stored_observed.get(key)

    derived_compared = compare(stored_derived, fresh_derived, "derived")
    observed_compared = compare(stored_observed, supplied, "observed")
    quote_patterns, quote_values = check_quotes(
        dict(stored_derived, **stored_observed))

    # ---- report --------------------------------------------------------------
    print("docs audit: %s (regression step 10)" % FACTS_REL)
    print("index: %d authoritative row(s) (%s)" % (
        sum(counts.values()),
        ", ".join("%d %s" % (n, s) for s, n in sorted(counts.items())) or "none"))
    print("links: %d local link(s) in %d entry document(s) resolved"
          % (checked_links, len(ENTRY_DOCS)))
    print("scan: %d markdown file(s), %d scaffold(s), %d task stub(s), 0 unmarked"
          % (files, scaffolds, tasks))
    print("derived: %d fact(s) re-read from their source" % derived_compared)
    if supplied:
        print("observed: %d fact(s) supplied by this run, %d compared"
              % (len(supplied), observed_compared))
    else:
        print("observed: not supplied (the regression runner passes them; %d in file)"
              % len(stored_observed))
    print("prose quotes: %d pattern(s), %d value(s) checked against the facts file"
          % (quote_patterns, quote_values))
    for line in changes:
        print("updated: %s" % line)

    if PROBLEMS:
        for group, message in PROBLEMS:
            print("FAIL [%s] %s" % (group, message))
        stale = {k: v for k, v in supplied.items()
                 if k not in stored_observed or stored_observed[k] != v}
        if stale and not args.update_facts:
            print("re-baseline deliberately: python 11_documentation/tools/docs_audit.py "
                  "--update-facts %s"
                  % " ".join("--observed %s=%s" % (k, v) for k, v in sorted(stale.items())))
        print("DOCS: FAIL (%d problem(s))" % len(PROBLEMS))
        return 1
    print("DOCS: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
