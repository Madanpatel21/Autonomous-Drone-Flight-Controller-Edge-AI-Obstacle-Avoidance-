#!/usr/bin/env python3
"""Release manifest audit - MFG-001 acceptance evidence (Phase 25).

Answers one question: does the manufacturing release package actually contain
what it claims, at the revision it claims, and is every missing piece blocked by
something real?

    python 12_manufacturing/tools/audit_release_manifest.py                 # check (used by the regression)
    python 12_manufacturing/tools/audit_release_manifest.py --update-digests --revision MFG-B

Checks
  1. structure: unique item ids, known statuses, required fields
  2. present items: file exists, digest matches, not a placeholder stub, lives in the package
  3. gated items: gate resolves to a requirement row that is NOT MET or to a
     declared facility gate; any directory declared empty really is empty
  4. facility gates: every declared blocker is still true (probes); a probe that
     no longer holds fails the audit, so a gate cannot rot into an excuse
  5. version bindings: FC/GS constants agree and match the released parameter file
  6. artifact checks: parameter blob re-derived independently; controlled BOM rules

Exit code 0 = PASS, 1 = FAIL. Warnings do not fail the run but are printed.
"""
import argparse
import csv
import hashlib
import json
import re
import shutil
import struct
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "12_manufacturing" / "RELEASE_MANIFEST.json"
REQUIREMENTS = ROOT / "00_project_control" / "02_requirements" / "REQUIREMENTS_DOMAINS.md"
VERIFICATION = ROOT / "08_testing" / "VERIFICATION_REPORT.md"
STUB_MARKER = "Purpose: define and store artifacts for this project area."


def use_root(root):
    """Point the audit at another tree (selftest_audit.py builds throwaway
    fixtures this way; production runs always use the repository root)."""
    global ROOT, MANIFEST, REQUIREMENTS, VERIFICATION
    ROOT = Path(root).resolve()
    MANIFEST = ROOT / "12_manufacturing" / "RELEASE_MANIFEST.json"
    REQUIREMENTS = ROOT / "00_project_control" / "02_requirements" / "REQUIREMENTS_DOMAINS.md"
    VERIFICATION = ROOT / "08_testing" / "VERIFICATION_REPORT.md"

REQ_STATUSES = {"present", "gated"}
PASS, FAIL, WARN = [], [], []


def ok(msg):
    PASS.append(msg)
    print("  [PASS] " + msg)


def fail(msg):
    FAIL.append(msg)
    print("  [FAIL] " + msg)


def warn(msg):
    WARN.append(msg)
    print("  [WARN] " + msg)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def requirement_rows():
    """-> {ID: status cell} from the verification report (status = last cell)."""
    rows = {}
    for line in VERIFICATION.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) >= 4 and re.fullmatch(r"[A-Z]+-[0-9]{3}", cells[0]):
            rows[cells[0]] = cells[-1]
    return rows


def requirement_exists(req_id):
    text = REQUIREMENTS.read_text(encoding="utf-8")
    return bool(re.search(r"^\|\s*" + re.escape(req_id) + r"\s*\|", text, re.M))


# --------------------------------------------------------------------------- 1
def check_structure(manifest):
    items = manifest.get("items", [])
    ids = [i.get("id") for i in items]
    if len(set(ids)) != len(ids):
        fail("structure: duplicate item ids")
    else:
        ok("structure: %d items, ids unique" % len(items))
    for item in items:
        iid = item.get("id", "?")
        if item.get("status") not in REQ_STATUSES:
            fail("structure: %s has status %r (allowed: %s)"
                 % (iid, item.get("status"), ", ".join(sorted(REQ_STATUSES))))
        if not item.get("item"):
            fail("structure: %s has no item name" % iid)
        if item["status"] == "present":
            for key in ("path", "sha256", "purpose"):
                if not item.get(key):
                    fail("structure: present item %s missing %r" % (iid, key))
        else:
            gates = item.get("gates") or item.get("facility_gates")
            if not gates:
                fail("structure: gated item %s names no gate" % iid)
            if not item.get("reason"):
                fail("structure: gated item %s has no reason" % iid)
    if not manifest.get("package", {}).get("revision"):
        fail("structure: package.revision missing")
    if not manifest.get("facility_gates"):
        fail("structure: no facility gates declared")


# --------------------------------------------------------------------------- 2
def check_present_items(manifest):
    bad = 0
    for item in manifest["items"]:
        if item["status"] != "present":
            continue
        iid, rel = item["id"], item["path"]
        path = ROOT / rel
        if not path.is_file():
            fail("present item %s: %s does not exist" % (iid, rel))
            bad += 1
            continue
        if not rel.startswith("12_manufacturing/"):
            fail("present item %s: %s is outside the release package - a package "
                 "must not claim a source file as its own artifact" % (iid, rel))
            bad += 1
        if path.stat().st_size == 0:
            fail("present item %s: %s is empty" % (iid, rel))
            bad += 1
            continue
        if path.suffix in (".md", ".txt", ".py", ".c", ".csv"):
            text = path.read_text(encoding="utf-8", errors="replace")
            if STUB_MARKER in text:
                fail("present item %s: %s is still a placeholder stub" % (iid, rel))
                bad += 1
                continue
        digest = sha256(path)
        if digest != item["sha256"]:
            fail("present item %s: digest mismatch for %s (recorded %s, actual %s) - "
                 "re-baseline with --update-digests and a revision bump"
                 % (iid, rel, item["sha256"][:12], digest[:12]))
            bad += 1
    if bad == 0:
        n = sum(1 for i in manifest["items"] if i["status"] == "present")
        ok("present items: %d files exist, digests match, none is a stub" % n)


# --------------------------------------------------------------------------- 3
def check_gated_items(manifest):
    rows = requirement_rows()
    bad = 0
    for item in manifest["items"]:
        if item["status"] != "gated":
            continue
        iid = item["id"]
        for req in item.get("gates", []):
            if not requirement_exists(req):
                fail("gated item %s: requirement %s does not exist" % (iid, req))
                bad += 1
                continue
            cell = rows.get(req, "")
            if not cell:
                fail("gated item %s: %s has no status row in VERIFICATION_REPORT.md" % (iid, req))
                bad += 1
                continue
            if not re.search(r"OPEN|PARTIAL", cell):
                fail("gated item %s: gate %s is no longer open (status: %s) - produce "
                     "the artifact or re-scope the item" % (iid, req, cell[:60]))
                bad += 1
            elif "MET" in cell.upper().replace("**", ""):
                warn("gated item %s: gate %s is partly MET (%s) - confirm the gate is "
                     "still the real obstruction" % (iid, req, cell[:50]))
        for gate in item.get("facility_gates", []):
            if gate not in manifest["facility_gates"]:
                fail("gated item %s: facility gate %s is not declared" % (iid, gate))
                bad += 1
        # a directory declared empty must really be empty (README only)
        if item.get("expect_empty"):
            target = ROOT / item["expected_path"].rstrip("/")
            if target.is_dir():
                stray = [p for p in sorted(target.iterdir())
                         if p.is_file() and p.name.lower() != "readme.md"]
                if stray:
                    fail("gated item %s: %s was declared empty but contains %s"
                         % (iid, item["expected_path"], ", ".join(p.name for p in stray)))
                    bad += 1
    if bad == 0:
        n = sum(1 for i in manifest["items"] if i["status"] == "gated")
        ok("gated items: %d, every gate resolves to a requirement that is not MET "
           "or to a declared facility gate" % n)


# --------------------------------------------------------------------------- 4
def check_facility_gates(manifest):
    bad = 0
    for name, gate in manifest["facility_gates"].items():
        if not gate.get("blocker") or not gate.get("resolved_when"):
            fail("facility gate %s: needs both a blocker and a resolved_when condition" % name)
            bad += 1
        probes = gate.get("probes") or []
        if not probes:
            fail("facility gate %s: declares no probe, so nothing can prove it is still open" % name)
            bad += 1
        for probe in probes:
            kind, _, arg = probe.partition(":")
            if kind == "toolchain_absent":
                if shutil.which(arg):
                    fail("facility gate %s: probe '%s' no longer holds - %s is installed; "
                         "produce the gated artifact" % (name, probe, arg))
                    bad += 1
            elif kind == "no_match":
                # a README inside a gated directory documents the gate, it is not
                # a release artifact
                hits = [h for h in ROOT.glob(arg)  # Path.glob handles ** natively
                        if h.name.lower() != "readme.md"]
                if hits:
                    fail("facility gate %s: probe '%s' no longer holds - matched %s; "
                         "produce the gated artifact"
                         % (name, probe, ", ".join(str(h.relative_to(ROOT)) for h in hits[:3])))
                    bad += 1
            else:
                fail("facility gate %s: unknown probe kind %r" % (name, kind))
                bad += 1
    if bad == 0:
        ok("facility gates: %d declared blockers still hold (probed)"
           % len(manifest["facility_gates"]))


# --------------------------------------------------------------------------- 5
def check_version_bindings(manifest):
    values, bad = {}, 0
    for b in manifest.get("version_bindings", []):
        path = ROOT / b["file"]
        if not path.is_file():
            fail("version binding %s: %s missing" % (b["name"], b["file"]))
            bad += 1
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        hits = re.findall(b["regex"], text)
        if len(hits) != 1:
            fail("version binding %s: pattern matched %d times in %s (need exactly 1)"
                 % (b["name"], len(hits), b["file"]))
            bad += 1
            continue
        values[b["name"]] = int(hits[0])
    for b in manifest.get("version_bindings", []):
        mine = values.get(b["name"])
        if mine is None:
            continue
        if "expect" in b and mine != b["expect"]:
            fail("version binding %s: found %s, manifest expects %s"
                 % (b["name"], mine, b["expect"]))
            bad += 1
        other = b.get("must_equal")
        if other:
            theirs = values.get(other)
            if theirs is None:
                fail("version binding %s: cannot compare against %s" % (b["name"], other))
                bad += 1
            elif mine != theirs:
                fail("version binding %s: %s = %s but %s = %s (they must agree)"
                     % (b["name"], b["name"], mine, other, theirs))
                bad += 1
    if bad == 0:
        ok("version bindings: %d checked, FC and GS agree, released file matches firmware"
           % len(values))


# --------------------------------------------------------------------------- 6
def crc16_ccitt(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def check_param_blob(check):
    """Re-derive the released blob instead of trusting the generator that made it."""
    blob_path, json_path = ROOT / check["blob"], ROOT / check["json"]
    if not blob_path.is_file() or not json_path.is_file():
        fail("%s: blob or field manifest missing" % check["id"])
        return
    meta = json.loads(json_path.read_text(encoding="utf-8"))
    raw = blob_path.read_bytes()
    bad = 0

    if len(raw) != meta["blob_len"] or len(raw) != meta["struct_size"] + 2:
        fail("%s: blob is %d bytes, field manifest says %d (%d + crc16)"
             % (check["id"], len(raw), meta["blob_len"], meta["struct_size"]))
        bad += 1
    body, stored = raw[:-2], raw[-2:]
    if crc16_ccitt(body) != struct.unpack("<H", stored)[0]:
        fail("%s: trailing CRC does not cover the blob" % check["id"])
        bad += 1
    elif crc16_ccitt(body) != meta["crc"]:
        fail("%s: CRC 0x%04X recomputed from the blob, field manifest says 0x%04X"
             % (check["id"], crc16_ccitt(body), meta["crc"]))
        bad += 1

    # re-read every field at the offset the C generator reported, and require the
    # JSON value to re-pack to exactly those bytes (float32 round-trip)
    for field in meta["field_layout"]:
        name, off = field["name"], field["offset"]
        want = meta["fields"].get(name)
        if want is None:
            fail("%s: field %s absent from the value map" % (check["id"], name))
            bad += 1
            continue
        if field["type"] == "u32":
            got, packed = struct.unpack_from("<I", body, off)[0], struct.pack("<I", want)
        elif field["type"] == "u16":
            got, packed = struct.unpack_from("<H", body, off)[0], struct.pack("<H", want)
        else:
            got, packed = struct.unpack_from("<f", body, off)[0], struct.pack("<f", want)
        if packed != body[off:off + len(packed)] or (field["type"] != "f32" and got != want):
            fail("%s: field %s at offset %d does not match the field manifest"
                 % (check["id"], name, off))
            bad += 1
    if bad == 0:
        ok("param default file: %d bytes, CRC 0x%04X re-derived, %d fields bit-exact, "
           "version %d" % (len(raw), meta["crc"], len(meta["field_layout"]),
                           meta["fields"]["version"]))


def check_bom(check, manifest):
    path = ROOT / check["csv"]
    if not path.is_file():
        fail("%s: %s missing" % (check["id"], check["csv"]))
        return
    lines = path.read_text(encoding="utf-8").splitlines()
    m = re.match(r"#\s*Revision:\s*(\S+)", lines[0] if lines else "")
    if not m:
        fail("%s: first line must declare '# Revision: <pkg-rev>'" % check["id"])
        return
    revision = m.group(1)
    bad = 0
    if revision != manifest["package"]["revision"]:
        fail("%s: BOM revision %s != package revision %s (a build would be released "
             "against the wrong BOM)" % (check["id"], revision, manifest["package"]["revision"]))
        bad += 1

    required = {"item", "ref_des", "function", "mpn", "manufacturer", "qty",
                "alternate", "status", "gate", "notes"}
    reader = csv.DictReader(lines[1:])
    if not required.issubset({c.strip() for c in (reader.fieldnames or [])}):
        fail("%s: missing columns %s" % (check["id"], sorted(required - set(reader.fieldnames or []))))
        return

    rows = requirement_rows()
    facilities = manifest["facility_gates"]
    seen, known_status = set(), {"selected", "provisional", "class-selected", "tbd"}
    unpinned = gated = 0
    for row in reader:
        iid = row["item"]
        seen.add(iid)
        if row["status"] not in known_status:
            fail("%s: %s status %r unknown" % (check["id"], iid, row["status"]))
            bad += 1
        if not re.fullmatch(r"[1-9][0-9]*", row["qty"]):
            fail("%s: %s qty %r is not a positive integer" % (check["id"], iid, row["qty"]))
            bad += 1
        if row["status"] in ("selected", "provisional"):
            if not row["mpn"] or not row["manufacturer"]:
                fail("%s: %s is %s but has no MPN/manufacturer" % (check["id"], iid, row["status"]))
                bad += 1
            if row["gate"]:
                fail("%s: %s is %s yet carries gate %s - a pinned part cannot be gated"
                     % (check["id"], iid, row["status"], row["gate"]))
                bad += 1
        else:
            unpinned += 1
            if not row["gate"]:
                fail("%s: %s is %s and names no gate (the rule: an unpinned row must "
                     "name what stops it)" % (check["id"], iid, row["status"]))
                bad += 1
            elif row["gate"] in facilities:
                gated += 1
            elif requirement_exists(row["gate"]) and re.search(
                    r"OPEN|PARTIAL", rows.get(row["gate"], "")):
                gated += 1
            else:
                fail("%s: %s gate %s is not a declared facility gate nor a requirement "
                     "whose row is still open" % (check["id"], iid, row["gate"]))
                bad += 1
        # ref_des vs qty: implied count may be smaller (spares allowed), never larger
        tokens = [t for t in re.split(r"[,\s]+", row["ref_des"]) if t]
        if tokens and int(row["qty"]) < len(tokens):
            fail("%s: %s qty %s is less than the %d ref-des entries"
                 % (check["id"], iid, row["qty"], len(tokens)))
            bad += 1
        if row["status"] == "provisional" and "OPEN" not in row["notes"].upper():
            warn("%s: %s is provisional but its note does not name the open element"
                 % (check["id"], iid))
    if bad == 0:
        ok("controlled BOM: %d rows, revision %s, %d unpinned rows all gated (%d by "
           "facility gate)" % (len(seen), revision, unpinned, gated))


# --------------------------------------------------------------------------- main
REV_LINE = re.compile(r"^(#\s*Revision:\s*)(\S+)\s*$", re.M)
TEXT_SUFFIXES = (".csv", ".md", ".json", ".txt", ".yaml")


def do_update(manifest, revision):
    # A revision bump must carry the embedded revision in every artifact that
    # declares one, or the package contradicts itself (the selftest caught the
    # first version of this function doing exactly that).
    rebadged = []
    for item in manifest["items"]:
        if item["status"] != "present":
            continue
        path = ROOT / item["path"]
        if path.suffix not in TEXT_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8")
        rebadged_text = REV_LINE.sub(lambda m: m.group(1) + revision, text, count=1)
        if rebadged_text != text:
            path.write_text(rebadged_text, encoding="utf-8")
            rebadged.append(item["id"])

    changed = []
    for item in manifest["items"]:
        if item["status"] != "present":
            continue
        digest = sha256(ROOT / item["path"])
        if digest != item["sha256"]:
            changed.append((item["id"], item["sha256"][:12] or "-", digest[:12]))
            item["sha256"] = digest
    for iid in rebadged:
        print("  %s: embedded revision line bumped (and re-digested)" % iid)
    print("re-baseline to %s: %d item(s) changed" % (revision, len(changed)))
    for iid, old, new in changed:
        print("  %s: %s -> %s" % (iid, old, new))
    manifest["package"]["revision"] = revision
    manifest["package"]["created"] = date.today().isoformat()
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    print("wrote %s" % MANIFEST.relative_to(ROOT))
    return 0


def main():
    ap = argparse.ArgumentParser(description="MFG-001 release manifest audit")
    ap.add_argument("--update-digests", action="store_true",
                    help="re-baseline package digests (release action; requires a revision bump)")
    ap.add_argument("--revision", help="new package revision, e.g. MFG-B")
    ap.add_argument("--root", help="audit another tree (used by selftest_audit.py)")
    args = ap.parse_args()

    if args.root:
        use_root(args.root)

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    pkg = manifest["package"]
    print("Release manifest audit - revision %s (%s)"
          % (pkg["revision"], MANIFEST.relative_to(ROOT)))

    if args.update_digests:
        if not args.revision:
            print("ERROR: --update-digests requires --revision <new revision>")
            return 2
        if args.revision == pkg["revision"]:
            blank = [i["id"] for i in manifest["items"]
                     if i["status"] == "present" and not i["sha256"]]
            if not blank:
                print("ERROR: --update-digests must bump the revision (%s is current); "
                      "a re-baseline without a new revision would hide a change"
                      % pkg["revision"])
                return 2
            print("note: initial baseline of revision %s (%d item(s) had no digest yet)"
                  % (pkg["revision"], len(blank)))
        return do_update(manifest, args.revision)
    if args.revision:
        print("ERROR: --revision without --update-digests does nothing")
        return 2

    check_structure(manifest)
    check_present_items(manifest)
    check_gated_items(manifest)
    check_facility_gates(manifest)
    check_version_bindings(manifest)
    for check in manifest.get("artifact_checks", []):
        if check["kind"] == "param_default_blob":
            check_param_blob(check)
        elif check["kind"] == "bom_csv":
            check_bom(check, manifest)
        else:
            fail("unknown artifact check kind %r" % check["kind"])

    print("AUDIT: %s (%d checks passed, %d failed, %d warnings)"
          % ("FAIL" if FAIL else "PASS", len(PASS), len(FAIL), len(WARN)))
    for msg in WARN:
        print("  warning: " + msg)
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
