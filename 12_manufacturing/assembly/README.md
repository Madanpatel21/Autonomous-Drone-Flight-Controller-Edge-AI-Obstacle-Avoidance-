# assembly

Sequence and drawing requirements for assembling one flight controller.

* [`ASSEMBLY_SEQUENCE.md`](ASSEMBLY_SEQUENCE.md) — the procedure, written now.
* The **assembly drawings themselves are gated** (`HW-010` + `EDA_TOOLCHAIN`):
  a drawing is a projection of a layout that does not exist yet. The drawing must
  come out of the same layout revision as the gerbers and be listed as a `present`
  manifest item with its digest before a build can reference it.
* Pick-and-place data lives with the fabrication package
  ([`../fabrication/FABRICATION_DATA_SPEC.md`](../fabrication/FABRICATION_DATA_SPEC.md));
  the assembly house gets the CSV, the assembly worker gets the drawing.

Assembly acceptance for a unit is recorded per serial number in
[`../qc/`](../qc/) — see [`../PROGRAMMING_AND_PRODUCTION_TEST.md`](../PROGRAMMING_AND_PRODUCTION_TEST.md)
for the record format and the serial-number grammar.
