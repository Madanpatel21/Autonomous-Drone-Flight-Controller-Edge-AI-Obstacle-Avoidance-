# Controlled BOM

This directory holds the **controlled** BOM for the flight-controller build:
[`CONTROLLED_BOM.csv`](CONTROLLED_BOM.csv). The Phase 04 draft in
`01_hardware/01_flight_controller/bom/BOM.md` is history; this file is what a
build is released against.

Machine-readable on purpose: `12_manufacturing/tools/audit_release_manifest.py`
parses this file on every regression run, so the rules below are enforced rather
than described.

## Status vocabulary (closed set)

| Status | Meaning | Extra obligation |
|---|---|---|
| `selected` | MPN chosen and pinned; acceptance evidence exists | MPN + manufacturer required |
| `provisional` | MPN chosen, one acceptance element still open on the hardware side | MPN + manufacturer required, note must name the open element |
| `class-selected` | Only a *class* is fixed (e.g. "2207 1750–2400KV") | `gate` required: what locks the part |
| `tbd` | No part/class pinned yet | `gate` required |

## The rule the audit enforces

**A row that does not name a part must name the gate that stops it.** Every
`class-selected` / `tbd` row carries a `gate` token which must resolve to
either:

* a requirement ID whose row in `08_testing/VERIFICATION_REPORT.md` is **not**
  MET (e.g. `HW-010`, `OPS-001`), or
* a facility gate declared in `RELEASE_MANIFEST.json` (e.g. `EDA_TOOLCHAIN`,
  `THRUST_STAND`).

An unresolvable gate, or a gate that has since been marked MET, fails the audit.
This is deliberate: the Phase 24 lesson was that a requirement exercised only
through a path production never uses still looked green. A "TBD" row with a
stale gate would be the same failure in a different costume.

Facility gates carry probes; if a probe shows the facility now exists (e.g. a
layout file appeared, or a thrust-stand measurement CSV appeared), the audit
fails and demands the artifact or a re-baseline instead of letting the blocker
sit there quietly.

## Alternates

`alternate` is `none-approved` for every row today. That is the honest state:
no alternate has been through electrical review, and a controlled BOM that lists
an unverified substitute is worse than one that lists none. To add one, put the
part in `alternate` and state in `notes` which review it passed (ERC/DFM/
pin-compatibility). Substitution without that note is a configuration escape and
is a nonconformance, not a convenience.

## Revision control

Line 1 is `# Revision: <MFG-x>` and must equal `package.revision` in
`RELEASE_MANIFEST.json`. Change a part, a quantity, a status or a gate → the
file's digest no longer matches the manifest and the audit fails until the
package is re-baselined with a revision bump. There is no git repository behind
this workspace, so the digest pin is the revision control.
