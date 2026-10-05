# drill_files

**Contents: nothing yet.** Drill data is generated from the same layout the
gerbers come from, so it shares the gate.

Gate: `HW-010` + facility gate `EDA_TOOLCHAIN`
(see [`../fabrication/FABRICATION_DATA_SPEC.md`](../fabrication/FABRICATION_DATA_SPEC.md)).

Expected here when released: Excellon drill file(s) with plated and non-plated
holes separated, a tool table, and the drill map. Via classes come from
`PCB_CONSTRAINTS.md`: 0.3/0.6 mm signal vias, 0.5/0.9 mm power stitching, M3
mounting holes on the 30.5 mm pattern, and the board-edge keep-out of ≥3 mm to
the first component — check those against the exported tool table, not against
memory.
