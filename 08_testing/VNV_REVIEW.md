# V&V Review — Phase 27

Formal requirement → design → implementation → test → evidence review, executed
by `08_testing/vnv_gate.py` (runner step 11) and narrated here. Response to
`15_prompts/27_27_vnv.prompt.md`.

**This review does not claim certification, airworthiness or flight readiness.**
It claims that every one of the 83 baseline requirements has been walked through
the chain, classified against its own evidence, and that every critical one is
either verified by executed evidence or explicitly blocked with the blocker
written down.

## 1. Inputs / assumptions

- Baseline: `00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md`
  (83 IDs, APPROVED 2026-10-03).
- Authoritative status source: `08_testing/VERIFICATION_REPORT.md` (DEC-020
  vocabulary, one row per ID). Traceability and implementation references:
  `00_project_control/01_governance/TRACEABILITY_MATRIX.md`.
- Evidence: the regression runner (**33 executed steps PASS + 2 H-gated SKIPs**
  as of Phase 29), the firmware suite (**2741/2741**), the ground-station
  tests (18), the host HIL rig (6 executed steps) and the audits.
- Classification rule (in the gate, so it cannot drift): the **last** status
  token in a verification row is the outcome, because compound rows put the
  caveat last — `**MET (review)**; board unbuilt → **OPEN (H)**` is a blocked
  completion element, not a verified requirement. Outcomes: `verified`,
  `partial`, `blocked` (`OPEN (H)`), `open`.
- **Critical** = the loss-of-control chain: `SAF-*` (failsafe hierarchy and
  action, IMU/deadline response, arming authority, battery bands, avoidance
  bounds), `SYS-001..004` (rates, latency, RC loss, perception fallback),
  `FW-002/003` (scheduler/watchdog, bus-fault containment), `EST-001`,
  `CTRL-001/002/003/005` (cascade, bounds, mixer, disarm), `NAV-004` (geofence),
  `COM-004`/`GS-003` (command authority boundary, safety action on the wire).
- Every reference a row puts in backticks that looks like a file path must
  resolve. Code tokens (`malloc/calloc/realloc/free`) and glob shorthand
  (`{bin,json}`) are excluded by construction.

## 2. Work performed

The gate re-derives the whole review from the documents and the filesystem on
every run. Current result, quoted verbatim by the gate's own check of this file:

**Gate result: 83 baseline requirements, 23 critical — 49 verified, 34 unverified
(11 partial, 18 blocked, 5 open), 0 failed review items.** 27 evidence
references checked across the requirement rows, 0 broken.

### Critical requirements — 23 of 23 verified or explicitly blocked

| Critical ID | Outcome | Verified by / blocked on |
|---|---|---|
| SAF-001 | verified | failability-ranked failsafe; `imu_rc_loss` asserted on the live log |
| SAF-002 | blocked | bounded actions exercised (SIM, GS decode); the *physical* motor-stop descent needs a board |
| SAF-003 | verified | stale-IMU abort; TEST-SIM-IMUFAIL + `imu_dropout` scenario |
| SAF-004 | blocked | watchdog negative test on host HIL; real IWDG needs a board |
| SAF-005 | open | arming class refused FC-side; the **live** command path does not exist (COM-004) |
| SAF-030 | verified | battery bands + hysteresis unit tests |
| SAF-031 | verified | latched RTL band flies home (mission RTL hook asserted) |
| SAF-040 | verified | avoidance clamp-last bounds, adversarial inputs |
| SAF-041 | verified | FC sensor wins in the cone; conflict clamp + ToF-only synthesis |
| SYS-001 | partial | software dispatch verified; MCU rate/latency needs HIL-3 capture |
| SYS-002 | blocked | end-to-end latency not measurable without a capture; single-tick p99 = 3 µs bounds the software side |
| SYS-003 | verified | RC loss → failsafe ≤500 ms in SIM fault injection |
| SYS-004 | verified | companion heartbeat timeout → FC-only modes (`ai_loss`) |
| FW-002 | verified | scheduler deadline monitoring; p99 ≤2 ms logged |
| FW-003 | blocked | bus faults contained (SIM); bus-level fault behaviour needs hardware |
| EST-001 | partial | complementary filter + gyro-bias, not the specified quaternion EKF; no magnetometer |
| CTRL-001 | verified | cascade PID + anti-windup, closed-loop tracking |
| CTRL-002 | blocked | rate divides + deadline monitor verified; hardware rates need HIL |
| CTRL-003 | verified | quad-X mixer, saturation minimisation |
| CTRL-005 | verified | disarm → zero output |
| NAV-004 | verified | geofence → RTL asserted |
| COM-004 | partial | FC-side gate proven; **no live transport** exists |
| GS-003 | partial | schema v2 decodes `action=MOTOR_STOP` end-to-end; live transport open |

No critical requirement is silently unverified: each non-verified row names its
blocker in the verification report, and the gate fails the run if it does not.

### Residual-risk register — 24 non-critical items (reported, not hidden)

| ID | Outcome | What remains |
|---|---|---|
| SYS-005 | blocked | brownout-safe behaviour on a real pack (analysis + SIM today) |
| SYS-007 | blocked | endurance by measurement (mass/thrust budget only) |
| HW-001/002/003/007/009/010 | blocked | design-time evidence complete; board/PCB measurements absent |
| HW-006 | partial | no PWM fallback implemented |
| HW-008 | partial | FC-side companion power cycle not implemented |
| FW-005 | blocked | on-chip flash endurance unmeasured |
| FW-006 | partial | no explicit logging overload-drop policy or measurement |
| FW-007 | open | bootloader does not exist |
| SEN-003 | blocked | 6-face calibration on hardware |
| SEN-005 | blocked | MCU interrupt latency unmeasured (tick execution measured: 23 µs worst case) |
| EST-003 | partial | optical flow is a NOT_READY stub; GNSS-only horizontal |
| CTRL-006 | partial | no runtime parameter-update path |
| COM-001 | blocked | radio path hardware-gated |
| GS-002 | partial | no live command transport |
| AI-001 | blocked | ≤100 ms sensor→message latency is a hardware/companion property |
| AI-003 | open | no model benchmarked; no latency number fabricated |
| AI-005 | open | no dataset or model exists |
| MFG-001 | partial | software package complete/audited; fabrication half gated |
| OPS-001 | partial | software/evidence release record exists (RC-2, audited); operational release gated on BOARD/STM32_TOOLCHAIN |

## 3. Files created / modified

Created: `08_testing/vnv_gate.py` (the review, executable), this review.
Modified: `08_testing/regression_tests/run_regression.sh` (step 11),
`08_testing/{VERIFICATION_REPORT.md,TEST_GENERATION.md,MASTER_VERIFICATION_PLAN.md}`,
`00_project_control/{PROJECT_STATUS.md,DECISION_LOG.md,01_governance/TRACEABILITY_MATRIX.md}`,
`PROJECT_CONTENT_MANIFEST.md`, `11_documentation/{DOCUMENTATION_INDEX.md,NEW_ENGINEER_WALKTHROUGH.md,PROJECT_FACTS.json,RELEASE_DOCUMENTATION_CHECKLIST.md}`.

## 4. Interfaces affected

None in firmware or on the wire. The gate is a review tool; it reads documents
and the filesystem and has no runtime interface.

## 5. Verification performed

- `python 08_testing/vnv_gate.py` → **`VNV: PASS`**, counts as quoted above
  (49 verified / 11 partial / 18 blocked / 5 open, 27 evidence references, 0 broken).
- `python 08_testing/vnv_gate.py --selftest` → **7/7 cases behaved as specified**:
  control (critical MET with evidence), critical PARTIAL with a written reason,
  critical OPEN (H) with a named blocker, critical PARTIAL with no reason (must
  fail), missing verification row (must fail), critical verified with an
  evidence path that resolves to nothing (must fail), non-critical OPEN as a
  residual risk (must pass). The fixture harness runs this same CLI as a
  subprocess, so the shipped code path is what is tested (DEC-023).
- The gate also verifies that **this file** quotes the current result: it fails
  the run if the numbers here disagree with what it just computed.
- Full regression with step 11: exit 0, **33 executed steps PASS + 2 H-gated
  SKIPs** (Phases 28/29 added steps 12/13; the classification is unchanged).

## 6. Acceptance criteria

| Phase 27 acceptance | Result |
|---|---|
| Requirement → design → implementation → test → evidence review performed | **DONE** — gate walks every row, expands combined IDs, resolves evidence references |
| Failed items identified | **0 failed items** — no critical requirement is unverified without a written blocker; 0 broken evidence references in 27 checked |
| Blocked items identified | **5 critical blocked** (SAF-002/004, SYS-002, FW-003, CTRL-002) — every one names its blocker; 28 blocked/partial/open items in total including non-critical |
| Residual risks identified | **24 non-critical rows** in the register above, plus the five UNDETECTED fault-tree branches carried openly by Phase 24 |
| **All critical requirements verified or explicitly blocked** | **MET** — 13 verified, 10 explicitly blocked/partial/open with written blockers |
| Certification | **NOT CLAIMED** — no hardware evidence exists; SIM/host-HIL evidence never substitutes for it |

## 7. Risks / TBDs

1. The classification depends on the verification report's status token being
   the last one in the row; a future row that puts a caveat *before* its verdict
   would be read as the more optimistic outcome. The rule is documented here and
   in the gate header, and the selftest pins the compound-row behaviour.
2. `blocked` is a review classification, not a schedule: SAF-005/COM-004/GS-002/GS-003
   are blocked on a command transport that is not planned as a board task, and
   FW-007 (bootloader) is a product gap rather than a facility gate.
3. The residual-risk register is deliberately wide (24 items). Reducing it
   requires a board, a companion target and a trained model — none of which this
   workspace has; hiding any of them to make the review look tighter would be
   the exact failure this phase exists to prevent.
4. The five UNDETECTED fault-tree branches (IMU bias, motor/ESC loss, power
   collapse, GNSS wrong fix, real MCU timing) remain outside the evidence
   boundary; they are carried by the safety case, not resolved here.

## 8. Next dependency

- **Phase 28 (release) consumes this review.** `OPS-001` is now **PARTIAL** — the
  software/evidence release record exists and is audited; an operational release
  stays gated. The release must state the residual-risk register exactly as it is
  — no closure by assertion.
- Phase 29 (final audit) should re-run this gate and compare; the gate is
  deterministic, so any delta is a document change and must be explained.
- A board, an `arm-none-eabi-gcc` toolchain and a companion target would move
  every `OPEN (H)` above from "blocked" to measurable; the gate would then
  require those rows to become verified or explain themselves.
