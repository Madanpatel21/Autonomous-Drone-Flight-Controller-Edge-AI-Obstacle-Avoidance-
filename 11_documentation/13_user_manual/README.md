# 13_user_manual — operator documentation

**There is no user manual, and this is deliberate.** A manual describes how to
operate a *released* product; nothing here has been released — `OPS-001` is OPEN
(flight-test readiness review not performed, no motor-off bench pass, no flight),
and `MFG-001` is PARTIAL with the fabrication half gated. Writing operating
instructions for a vehicle that cannot be built yet would be the most dangerous
kind of documentation in this repository.

## What exists for an operator today

| For | Document |
|---|---|
| Ground station CLI (decode logs, inspect health, validate commands) | [`06_communication/ground_station/GROUND_STATION.md`](../../06_communication/ground_station/GROUND_STATION.md) |
| Running a unit on the bench: programming, serial, PT stations | [`12_manufacturing/PROGRAMMING_AND_PRODUCTION_TEST.md`](../../12_manufacturing/PROGRAMMING_AND_PRODUCTION_TEST.md) |
| What a flying unit would require before release | `OPS-001`; [`00_project_control/04_safety/SAFETY_CASE.md`](../../00_project_control/04_safety/SAFETY_CASE.md) non-claims |
| Safety restrictions that survive into operation | `SAF-*` requirements + the UNDETECTED branch list in the fault tree |

## What the manual must contain when it is written

Scope is fixed by the Phase 19 documentation prompts
(`15_prompts/19_documentation/02_user_manual.prompt.md`) and gated on the release
record (`OPS-001`, Phase 28): pre-flight checks, arming/disarming, mission upload,
failsafe behaviour **including what each reported action means to an operator**,
battery and geofence limits, emergency procedures, and troubleshooting.

The operator-facing half of that already exists in verifiable form: the safety
actions and their meanings are implemented and tested (`DEC-021`), and the ground
station renders them. Everything else waits for a vehicle that can fly.
