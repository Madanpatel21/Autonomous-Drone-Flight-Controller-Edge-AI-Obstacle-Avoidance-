# Assembly Sequence (one flight controller)

Written while the drawing is still CAD-gated. The sequence, the order of the
checks, and what is verified at each step do not depend on CAD; only the drawings
and mechanical dimensions do. Anything below marked **(CAD)** needs the released
assembly drawing before it can be executed.

## Before touching a board

0. Verify the board is from the released package: silkscreen revision matches
   `package.revision` in [`../RELEASE_MANIFEST.json`](../RELEASE_MANIFEST.json),
   and the incoming inspection in [`../pcb_inspection/`](../pcb_inspection/)
   recorded a pass for the panel it came off.

## Sides

The 4-layer board is assembled **power/small-signal side first**, because the IMU
zone on L1 constrains reflow of the middle of the board and the soft-mount posts
set later cannot survive a second full reflow.

### Pass 1 — bottom (L4): connectors, ESC harness, passives

1. Stencil print (paste geometry from the fabrication package).
2. Place the ESC/connector-side parts. **(CAD)**
3. Reflow. Inspect per A3 (X-ray only where the criteria demand it).
4. Hand-solder anything the drawing marks as hand-solder, then clean.

### Pass 2 — top (L1): MCU, IMU, baro, regulators

5. Place U1 (MCU, LQFP100), U2 (IMU, LGA-14), U3 (baro), U8 (INA226), the buck
   regulators and their passives.
6. **IMU rule before reflow:** U2 sits at the board centre, in the plane of prop
   rotation, with the 8 mm keep-out kept clear of other parts and the via fence
   around its zone. Check the placement against the drawing now — after reflow it
   is a rework job. **(CAD)**
7. Reflow. Inspect A3–A5, A7.
8. Attach the IMU soft silicone posts and the 3 mm standoff *after* the board is
   cool; do not reflow a board that already has the posts fitted.

### Pass 3 — connectors, protection, RF

9. Fit the protection set (D1 TVS, F1 fuse, Q1 reverse MOS) and the keyed
   connector set. Keying and polarity per ICD-05 — a connector that can be
   mated backwards is a nonconformance even if it works. **(CAD)**
10. Fit the GNSS module and keep its feed clear of the DShot/ESC area.
11. Clean, then run the **pre-power** checks from
    [`../PROGRAMMING_AND_PRODUCTION_TEST.md`](../PROGRAMMING_AND_PRODUCTION_TEST.md)
    (continuity, no shorts across rails, connector orientation) before a battery
    or bench supply is connected.

## Handling rules

* ESD: wrist strap on a common ground for every step; the IMU and MCU are
  unprotected once the bag is open.
* No powered assembly: nothing is inserted or removed while a supply is
  connected, including the companion link.
* Motors and props are **not** fitted for any electrical test in this sequence —
  props go on only at the flight-test stage under `OPS-001`.

## Honest status

Steps 2, 6, 9 and the whole of pass 1's drawing reference are gated on the
layout (`HW-010` + `EDA_TOOLCHAIN`). This sequence has never been executed on
hardware; it is a procedure, and the first build is also its own validation.
