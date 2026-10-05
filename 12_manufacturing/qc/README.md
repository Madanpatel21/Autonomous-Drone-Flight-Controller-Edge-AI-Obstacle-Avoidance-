# qc

Quality records for built units, one JSON record per serial number.

**Contents: nothing yet** — no unit has been built. `records/` is the directory
that must stay empty until a real board is serialized and tested; the manifest
audit probes it (`BOARD` facility gate) and fails if a record appears while the
gate is declared open, or if the gate is declared open while records exist.

Record filename: `<serial>.json`, serial grammar
`<MFG-rev>-<YYWW>-<NNNN>` (e.g. `MFGA-2640-0001`), so every unit traces to the
exact release package it was built and programmed from.

Minimum record fields (per [`../PROGRAMMING_AND_PRODUCTION_TEST.md`](../PROGRAMMING_AND_PRODUCTION_TEST.md)):
`serial`, `package_revision`, `bom_digest`, `param_blob_crc`, `firmware_digest`,
`station_results` (one entry per PT station with pass/fail and measured value),
`date`, `operator`. A record missing a station result is not a pass — it is an
incomplete test, and the unit is not releasable.
