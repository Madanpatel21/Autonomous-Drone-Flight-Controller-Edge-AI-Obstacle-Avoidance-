# release

Package staging for the manufacturing release.

**Contents: nothing yet.** A release here means: all `present` manifest items
digest-matched, all gated items either closed or explicitly accepted with the
gate named, and the package revision bumped. Until then this directory is empty
on purpose — an empty directory is an honest statement, a folder of drafts is
not.

Ownership: Phase 25 (this phase) makes the package reproducible and auditable;
**Phase 28 owns the operational release** (requirement `OPS-001`, currently
OPEN): flight-test readiness review sign-off, motor-off bench pass, geofence and
RC-failsafe verification. The release record for OPS-001 lands here as
`RELEASE_RECORD_<revision>.md` and is listed as a `present` manifest item.

Do not stage firmware images here until the STM32 toolchain gate is closed; the
binary item in the manifest is gated for exactly that reason.
