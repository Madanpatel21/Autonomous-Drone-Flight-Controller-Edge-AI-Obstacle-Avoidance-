# pcb_inspection

Inspection of the fabricated PCB and of the assembled unit.

* [`INSPECTION_CRITERIA.md`](INSPECTION_CRITERIA.md) — accept/reject criteria
  derived from the approved constraints. Written now, executable when boards exist.
* Incoming inspection records, AOI/X-ray output and the DRC report land here.
* **Records: none.** No board has been fabricated in this workspace, so there is
  nothing to inspect. This directory must not contain a fabricated inspection
  record; the manifest audit treats an inspection record without a serial number
  as a defect.

Gate for the records: `HW-010` + facility gate `BOARD`.
