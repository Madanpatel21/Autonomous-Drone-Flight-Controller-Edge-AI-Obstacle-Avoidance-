# Fabrication Data Specification

Status: contract written (Phase 25). **The data below does not exist yet** — there
is no approved layout, so gerbers/drill/pick-and-place cannot be exported. This
document is the specification the export must satisfy when the layout gate opens;
it is not a claim that a PCB has been fabricated.

Gate: `HW-010` (OPEN (H) — no PCB layout exists yet) and facility gate
`EDA_TOOLCHAIN` (no schematic/PCB CAD tool in this workspace). Both are live in
[`RELEASE_MANIFEST.json`](../RELEASE_MANIFEST.json) and are probed by the manifest
audit, so this document cannot outlive its own blocker unnoticed.

## What must be exported, from what

Source of truth for geometry: the approved layout file (`01_hardware/01_flight_controller/pcb/board/`).
Source of truth for constraints: [`PCB_CONSTRAINTS.md`](../../01_hardware/01_flight_controller/pcb/constraints/PCB_CONSTRAINTS.md)
(rev 2026-10-03, APPROVED) — stackup, class rules, IMU isolation, DShot/SPI rules.

| Export | Destination | Required format |
|---|---|---|
| Gerbers, one file per layer | `../gerbers/` | RS-274X, inches or mm stated in the job file, 4.6 format, units consistent across layers |
| Drill + drill map | `../drill_files/` | Excellon, plated/non-plated separated, tool table included |
| Pick-and-place (top, bottom) | `../pcb_inspection/` and the assembly house package | CSV with `RefDes,Layer,Footprint,CenterX(mm),CenterY(mm),Rotation(deg)`; origin = board origin as declared in the job file |
| Stencil aperture data | `../release/stencil/` | per-panel, with paste-reduction notes for the QFN/LGA pads |
| Assembly drawing (top/bottom) | `../assembly/` | PDF from the layout, ref-des silkscreen legible at 100 % |
| 3D STEP | `../release/` | for enclosure/mechanical fit checks only |
| DRC/ERC report | `../pcb_inspection/` | the tool's own report, unedited, plus the constraint file used |

## Layer set (4-layer, from PCB_CONSTRAINTS rev APPROVED)

`L1 top copper`, `L2 GND`, `L3 PWR`, `L4 bottom copper`, top/bottom solder mask,
top/bottom silkscreen, board outline, and **separately exported** paste layers for
stencil generation. A missing layer is a rejected package — the audit checks the
count, not just the presence of "some" gerber.

## Naming and revision

`FC30x30-<package-revision>-<layer>.<ext>`, e.g. `FC30x30-MFGA-L2_GND.gbr`. The
package revision in every filename must match `package.revision` in
`RELEASE_MANIFEST.json`. A fabrication package whose filenames do not carry the
package revision is rejected: that is how assemblies end up built from a
superseded layout.

## Acceptance (what "fabrication data released" will mean)

1. Every export in the table above exists and is non-empty.
2. The DRC report is the tool's raw output and shows **zero** clearance/width
   violations against the constraint file; any waiver is listed explicitly with
   the reason and is a management acceptance, not a silent pass.
3. The class rules in `PCB_CONSTRAINTS.md` are the ones the export was checked
   against — the constraint file's digest is recorded in the manifest when the
   export lands, so a later constraint change invalidates the check.
4. Digest re-baseline: each export is added to `RELEASE_MANIFEST.json` as a
   `present` item with sha256, and the manifest revision is bumped.

## Honest limitation

Until items 1–4 run on a real layout, the manufacturing package is
**software-complete and fabrication-incomplete**. Downstream (Phase 28 release
record, OPS-001) must not treat the existence of this specification as a released
board.
