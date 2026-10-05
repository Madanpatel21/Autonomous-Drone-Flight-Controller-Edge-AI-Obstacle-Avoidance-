# 02_hardware — hardware documentation

| What | Where |
|---|---|
| Component selection with datasheet references | [`01_hardware/00_system/COMPONENT_SELECTION.md`](../../01_hardware/00_system/COMPONENT_SELECTION.md) |
| Hardware architecture and power tree | [`MASTER_HARDWARE_ARCHITECTURE.md`](../../01_hardware/00_system/MASTER_HARDWARE_ARCHITECTURE.md), [`02_power/POWER_ARCHITECTURE.md`](../../01_hardware/02_power/POWER_ARCHITECTURE.md), [`POWER_BUDGET.md`](../../01_hardware/00_system/POWER_BUDGET.md) |
| PCB constraints (APPROVED): stackup, class rules, IMU isolation | [`pcb/constraints/PCB_CONSTRAINTS.md`](../../01_hardware/01_flight_controller/pcb/constraints/PCB_CONSTRAINTS.md) |
| Pin map / net intent | [`pcb/PCB_DESIGN_RECORD.md`](../../01_hardware/01_flight_controller/pcb/PCB_DESIGN_RECORD.md) |
| Draft BOM (Phase 04 history — the controlled BOM supersedes it) | [`bom/BOM.md`](../../01_hardware/01_flight_controller/bom/BOM.md) |
| Bring-up procedure and its execution record | [`BRINGUP_PLAN.md`](../../01_hardware/01_flight_controller/BRINGUP_PLAN.md), [`BRINGUP_EXECUTION_RECORD.md`](../../01_hardware/01_flight_controller/BRINGUP_EXECUTION_RECORD.md) |
| Review checklists to run at CAD completion | [`SCHEMATIC_REVIEW_CHECKLIST.md`](../../01_hardware/01_flight_controller/SCHEMATIC_REVIEW_CHECKLIST.md), [`PCB_REVIEW_CHECKLIST.md`](../../01_hardware/01_flight_controller/PCB_REVIEW_CHECKLIST.md) |

## State of this area (read before trusting any hardware statement)

**No board exists.** There is no schematic capture, no layout, no gerber and no
EDA tool in this workspace, so every hardware artifact here is a *specification*
or a *constraint*, and the ones that would require CAD are gated on
`EDA_TOOLCHAIN`/`HW-009`/`HW-010` in
[`12_manufacturing/RELEASE_MANIFEST.json`](../../12_manufacturing/RELEASE_MANIFEST.json).

Two consequences a new engineer should internalise:

* `BRINGUP_EXECUTION_RECORD.md` records **measurements NOT EXECUTED**. Nothing in
  it has been measured.
* Hardware requirements `HW-009`/`HW-010` are **OPEN (H)**; the PCB design record
  is a pin map, not a layout. Firmware that touches hardware is verified against
  a simulated backend, never against a device (see
  [`03_firmware/README.md`](../03_firmware/README.md)).

## Read next

* What the build must satisfy: [`../01_system/README.md`](../01_system/README.md)
* Manufacturing package and its gates: [`../11_manufacturing/README.md`](../11_manufacturing/README.md)
