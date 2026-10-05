# gerbers

**Contents: nothing yet — deliberately.** No gerber file exists, because no
approved layout exists. Writing placeholder gerbers here would be the single
most dangerous thing this project could do: a fabrication house will happily
build a board from plausible-looking copper.

Gate: `HW-010` (open) + facility gate `EDA_TOOLCHAIN`. Export requirements and
acceptance: [`../fabrication/FABRICATION_DATA_SPEC.md`](../fabrication/FABRICATION_DATA_SPEC.md).

Expected here when released: one RS-274X file per layer (L1–L4, mask ×2,
silkscreen ×2, outline, paste ×2), named
`FC30x30-<package-revision>-<layer>.<ext>`, plus the job file that declares units
and the 4.6 coordinate format.
