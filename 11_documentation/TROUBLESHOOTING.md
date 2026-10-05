# Troubleshooting

Symptom → what it means → what to do. Every entry here is a failure that actually
happened in this repository; the phase that found it is named so you can read the
full story in [`08_testing/TEST_GENERATION.md`](../08_testing/TEST_GENERATION.md)
or the decision log.

## The regression suite

| Symptom | Meaning | Do this |
|---|---|---|
| `unit+integration suite` FAIL | A case failed. The suite prints the case's own description before the summary. | Read the last descriptive line before `N/M passed`; that names the behaviour, not the test function. |
| `scenario X: got [...] expected [...]` | The mission did not reach the asserted nav sequence. | Check *when* the fault is injected: faults scheduled at `t=0` prevent arming and verify nothing (`DEC-010`) — they must land in flight. |
| `scenario X: deadline_misses=7` | A control tick overran its 900 µs budget. | Something blocking got added to the control path. `FW-003` requires non-blocking HAL calls; a `Sleep`, a flash write or a blocking read inside a tick will do this. |
| Determinism double-run diff non-zero | Two runs with the same seed differ. | Look for wall-clock time, uninitialised state, or iteration over unordered data. This check found a `vz` state random-walk from baro noise (Phase 16). |
| `HIL watchdog interlock` FAIL, `fc exit 0` | The emulated IWDG did **not** expire when starved. | The interlock is not armed. Do not "fix" the test: a watchdog that does not fire is the defect (`SAF-004`). |
| `HIL link robustness` FAIL | 18 corrupted frames were not all rejected. | The link CRC or the resync path is wrong. A false start-of-frame byte must not wedge the stream (Phase 22: an unvalidated length field did exactly that). |
| A HIL run hangs | Rig and FC are waiting for each other. | Start the **rig first**; the FC retries. Also check for `Sleep(1)` on Windows — it quantises to ~15.6 ms, which blew a flash-exchange timeout in Phase 22; use `select()`. |
| `stm32 backend build` / `HIL hardware execution` SKIP | Expected. No toolchain, no board. | Nothing to fix. These are never passes, and never failures. |

## The safety model

| Symptom | Meaning | Do this |
|---|---|---|
| `safety: failsafe=1 action=4` (RC loss reported, motor stop taken) | **Correct**, not a contradiction: the IMU died first, so the vehicle cannot fly. | Nothing. The action is ranked by flyability (`DEC-021`), and this is the case that model exists for. |
| An action appears while the reported failsafe looks benign | The two fields answer different questions: *why* control was taken, and *what the vehicle can still do*. | Read `failsafe_sm.c`'s action table, not the failsafe name. |
| Companion loss raised a failsafe | It must not. Companion loss is advisory-only (`DEC-007`). | Check the tier set: only RC, IMU, battery, geofence and estimator belong there. |
| The vehicle reported RTL but descended where it was | The battery RTL band latched but did not reach the mission. | This was a real defect (Phase 24, defect 4): the band was only ever exercised through a helper the application never calls. Assert safety behaviour on the live log, not on a helper. |

## The audits

| Symptom | Meaning | Do this |
|---|---|---|
| `present item … digest mismatch` | Someone edited a digest-pinned package file. | Re-baseline deliberately: `python 12_manufacturing/tools/audit_release_manifest.py --update-digests --revision MFG-B`. Never silence it by blanking the digest. |
| `facility gate X: probe … no longer holds` | The blocker is gone — a toolchain got installed or a layout appeared. | Produce the gated artifact. The gate was designed to fail the moment this happens. |
| `BOM revision MFG-A != package revision MFG-B` | A revision bump did not carry into the BOM. | Re-run the re-baseline (it rewrites embedded revision lines); if it still fails, the BOM was hand-edited. |
| `version binding … they must agree` | Flight controller and ground station disagree on a wire format. | Fix the decoder before touching the encoder. A decoder behind its encoder is how telemetry is lost silently. |
| `requirements without a verification status: CMD-002` | A requirement was added without a status row. | Add its row to `VERIFICATION_REPORT.md` and a traceability row. The audit exists so this cannot be forgotten. |
| docs audit: `unmarked placeholder` | A new file was created from a template and never filled in. | Run `python 11_documentation/tools/mark_scaffolds.py --write`, or write the content. |
| docs audit: `facts` mismatch | A number in the facts file is behind reality (the failing line names both values). | Re-baseline deliberately with `python 11_documentation/tools/docs_audit.py --update-facts --observed <key>=<value>` (the tool prints the exact arguments) — and say why. |

## The ground station

| Symptom | Meaning | Do this |
|---|---|---|
| `0 frames decoded` from a real log | CRC mismatch on every frame. | Use the protocol's reference nibble-accumulate CRC. A 256-entry byte-table rewrite is *not* bit-identical to MAVLink's CRC-16/MCRF4XX and silently rejected every real frame (Phase 20). |
| Phantom sequence gaps | Two streams share a counter. | MAVLink and the status record need separate counters (Phase 20: a shared counter produced 79 phantom gaps). |
| `UNKNOWN(n)` in the status output | An action or failsafe code the station does not know. | Intended: unknown codes are surfaced, never dropped to `NONE`. Extend the mapping in `gs_protocol.py`. |
| `--strict` exits 1 on a healthy log | The classifier saw WARN/CRITICAL. | Read the CRITICAL line; if the state is genuinely unhealthy, the exit code is correct behaviour, not a bug. |

## Documentation and numbers

| Symptom | Meaning | Do this |
|---|---|---|
| A document disagrees with the code | The version bindings and the docs audit are the arbiters. | Trust `telemetry.h`/`gs_protocol.py` and the manifest audit; then fix the document. |
| A doc you are editing turns the build red | It is digest-pinned or fact-pinned. | Use the documented re-baseline command for that artifact — that friction is the only revision control this workspace has. |
| You are unsure whether a document is real | Check for the two markers. | `**Scaffold — no content yet.**` and `**Task stub — not a record.**` mean: do not cite this as evidence. |

## When nothing here matches

The failure is new, and that is useful: write it down in
[`08_testing/TEST_GENERATION.md`](../08_testing/TEST_GENERATION.md) with the
defect, the fix and what would have caught it earlier. That file is the reason
this page exists.
