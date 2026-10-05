#!/usr/bin/env python3
"""Mark every placeholder document in the repository as a scaffold (Phase 26).

The repository was scaffolded in Phase 01 with one README per topic directory.
The vast majority still contain a single placeholder sentence, which reads like
documentation but is not: a new engineer cannot tell a real document from an
empty promise, and that ambiguity is the contradiction Phase 26 has to remove.

This tool replaces the placeholder with an explicit, uniform banner that says
what the file is (nothing), where real content lives, and that the status is
machine-checked. It is deliberately conservative: a file is only touched when
its *entire* body is a placeholder sentence, so a real document that happens to
mention the phrase is never clobbered. It is idempotent — re-running changes
nothing.

    python 11_documentation/tools/mark_scaffolds.py            # dry run, prints a summary
    python 11_documentation/tools/mark_scaffolds.py --write    # apply

Audit: 11_documentation/tools/docs_audit.py (regression step 10) fails if any
unmarked placeholder remains, so this cannot silently regress.
"""
import argparse
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INDEX_REL = "11_documentation/DOCUMENTATION_INDEX.md"
BANNER_MARKER = "**Scaffold — no content yet.**"

PLACEHOLDERS = (
    "Purpose: define and store artifacts for this project area.",
    "Project workspace section.",
)

# A second, subtler class: a title plus one imperative sentence. These were the
# scaffolding's *task descriptions* ("Define ...", "Measure ...", "Document ..."),
# and they read like records once the project has phases behind it: a newcomer
# opening 08_testing/FLIGHT_TEST_READINESS_REVIEW.md sees a review checklist and
# may believe a review happened. Detected strictly - short, no table, no code, no
# link, every remaining line an imperative - so a genuinely thin summary that
# states facts (e.g. the control/data flow) is not mislabelled.
IMPERATIVES = ("Define", "Document", "Measure", "Verify", "Record", "Establish",
               "Specify", "List", "Provide", "Describe", "Capture", "Maintain",
               "Track", "Produce", "Add", "Before any", "Include")
TASK_BANNER_MARKER = "**Task stub — not a record.**"

BANNER = """# {title}

> {marker} This directory exists so the repository layout is stable, not because
> an artifact was produced here. Nothing in this file is a finding, an instruction
> or evidence.
>
> **Where the real content lives:** [{index}]({rel}) — the index names the
> authoritative document for each area, and which parts are still gated.
>
> Scaffold status is machine-checked: `11_documentation/tools/docs_audit.py`
> (regression step 10) fails if an unmarked placeholder is left in the repository.
"""


def is_placeholder(text):
    """True only when the entire document is an H1 plus one placeholder sentence."""
    body = [ln.strip() for ln in text.splitlines() if ln.strip()]
    if len(body) != 2 or not body[0].startswith("# "):
        return False
    return body[1] in PLACEHOLDERS


def is_task_stub(text):
    """A title plus imperative instructions, and nothing that could be evidence.

    Deliberately conservative: any table, fenced block or link disqualifies the
    file, and every remaining line must open with an imperative verb, so a short
    factual summary is left alone.
    """
    if "```" in text or "](" in text or "|" in text:
        return False
    body = [ln.strip() for ln in text.splitlines() if ln.strip()]
    if len(body) < 2 or len(body) > 6 or not body[0].startswith("# "):
        return False
    return all(ln.startswith(IMPERATIVES) or ln.endswith("here.") for ln in body[1:])


def banner_for(path):
    title = path.parent.name or "README"
    rel = os.path.relpath(ROOT / INDEX_REL, path.parent).replace(os.sep, "/")
    return BANNER.format(title=title, marker=BANNER_MARKER, index=Path(INDEX_REL).name,
                         rel=rel)


def task_banner_for(path, text):
    """Keep the original task text (it is useful scope) but label it honestly."""
    body = [ln.strip() for ln in text.splitlines() if ln.strip()]
    rel = os.path.relpath(ROOT / INDEX_REL, path.parent).replace(os.sep, "/")
    banner = ("> {marker} The text below states what this document must cover once the\n"
              "> work it describes exists. Nothing here is a result, a measurement or a\n"
              "> record, and it must not be cited as evidence.\n"
              ">\n"
              "> Index and current status: [{index}]({rel})\n"
              .format(marker=TASK_BANNER_MARKER, index=Path(INDEX_REL).name, rel=rel))
    return body[0] + "\n\n" + banner + "\n" + "\n\n".join(body[1:]) + "\n"


def main():
    ap = argparse.ArgumentParser(description="mark placeholder docs as scaffolds")
    ap.add_argument("--write", action="store_true", help="apply the change (default: dry run)")
    args = ap.parse_args()

    marked, tasks, skipped = [], [], []
    for path in sorted(ROOT.rglob("*.md")):
        if any(part in ("build", "build-hil", ".git") for part in path.parts):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if BANNER_MARKER in text or TASK_BANNER_MARKER in text:
            skipped.append((path, "already marked"))
            continue
        if is_placeholder(text):
            marked.append(path)
            if args.write:
                path.write_text(banner_for(path), encoding="utf-8")
        elif is_task_stub(text):
            tasks.append(path)
            if args.write:
                path.write_text(task_banner_for(path, text), encoding="utf-8")
        else:
            skipped.append((path, "has content"))

    rel = lambda p: str(p.relative_to(ROOT)).replace(os.sep, "/")
    for path in marked:
        print("SCAFFOLD  %s" % rel(path))
    for path in tasks:
        print("TASK      %s" % rel(path))
    verb = "written" if args.write else "that would be written"
    print("%s: %d scaffold(s), %d task stub(s); skipped: %d (already marked or real content)"
          % (verb, len(marked), len(tasks), len(skipped)))
    if not args.write and marked:
        print("re-run with --write to apply")
    return 0


if __name__ == "__main__":
    sys.exit(main())
