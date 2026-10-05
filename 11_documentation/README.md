# 11_documentation

Entry point for anyone joining this repository. Phase 26 rebuilt this section
around one question: **can a new engineer tell what is real without reading
everything?**

## Start here

| If you want to… | Read |
|---|---|
| Get it building and running | [`NEW_ENGINEER_WALKTHROUGH.md`](NEW_ENGINEER_WALKTHROUGH.md) — every command verified in this workspace |
| Know what every document is worth | [`DOCUMENTATION_INDEX.md`](DOCUMENTATION_INDEX.md) — authoritative documents, plus the scaffold/stub policy |
| Understand the domain you are touching | the domain indexes: [system](01_system/README.md), [hardware](02_hardware/README.md), [firmware](03_firmware/README.md), [fusion](04_sensor_fusion/README.md), [control](05_flight_control/README.md), [navigation](06_navigation/README.md), [edge AI](07_edge_ai/README.md), [comms](08_communication/README.md), [simulation](09_simulation/README.md), [testing](10_testing/README.md), [manufacturing](11_manufacturing/README.md), [safety](12_safety/README.md), [operator](13_user_manual/README.md) |
| Fix something that is failing | [`TROUBLESHOOTING.md`](TROUBLESHOOTING.md) — symptoms from defects that actually happened |
| Check a number before quoting it | [`PROJECT_FACTS.json`](PROJECT_FACTS.json) — single source of truth, re-verified every regression run |
| Ship a release | [`RELEASE_DOCUMENTATION_CHECKLIST.md`](RELEASE_DOCUMENTATION_CHECKLIST.md) |

## The two honesty mechanisms in this section

1. **Documents declare what they are.** Anything empty carries
   `**Scaffold — no content yet.**`; anything that only states what a document
   *must cover* carries `**Task stub — not a record.**` Neither may be cited as
   evidence. `tools/docs_audit.py` fails the regression if an unmarked placeholder
   or instruction-only stub appears anywhere in the repository, so a template
   copied and forgotten cannot masquerade as content.
2. **Numbers have one home.** `PROJECT_FACTS.json` holds the counts that appear in
   prose (suite size, executed steps, GS tests, versions, package revision). The
   regression re-derives them and fails on disagreement, which is how the stale
   counts found in Phase 25 stop being possible.

## Tools

```bash
python 11_documentation/tools/docs_audit.py                 # index, links, markers, facts
python 11_documentation/tools/docs_audit.py --update-facts --observed k=v ...
                                                            # deliberate re-baseline
python 11_documentation/tools/mark_scaffolds.py             # dry run: what is still unmarked
python 11_documentation/tools/mark_scaffolds.py --write     # mark it
```

The regression runs `docs_audit.py` as step 10, so a marker that goes missing or a
number that drifts fails the build; marking new files is the manual step
(`mark_scaffolds.py`), and the audit makes sure it happened.
