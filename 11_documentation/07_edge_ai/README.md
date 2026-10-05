# 07_edge_ai — perception, avoidance and the companion AI

| What | Where |
|---|---|
| Perception fusion: source validity, degraded modes, FC-sensor priority | [`03_edge_ai/perception/PERCEPTION_FUSION.md`](../../03_edge_ai/perception/PERCEPTION_FUSION.md) |
| Obstacle avoidance policy and its hard bounds | [`03_edge_ai/avoidance/OBSTACLE_AVOIDANCE.md`](../../03_edge_ai/avoidance/OBSTACLE_AVOIDANCE.md) |
| Companion link contract (ICD-02 frames) | [`06_communication/COMMUNICATION_DESIGN.md`](../../06_communication/COMMUNICATION_DESIGN.md), `DEC-003`, `DEC-015` |
| The rule that keeps AI out of the safety path | `DEC-007`: AI publishes *detections only*, the FC applies them inside bounded limits |
| Simulated companion that produces real frames | `02_firmware/hal/sim/` (SIM companion emits ICD-02 HEARTBEAT/OBSTACLE_SET/HEALTH) |

Code: `flight_controller/communication/companion_link.c`,
`flight_controller/perception/perception_fusion.c`,
`flight_controller/avoidance/obstacle_avoidance.c`.

## Status — read this before quoting the AI

**There is no AI model, no dataset and no inference benchmark in this
repository.** `AI-003` and `AI-005` are **OPEN**, and no latency number exists
anywhere in this project because none was measured — inventing one would be the
single most misleading thing this documentation set could contain.

What *is* real and verified is the FC-side contract around the AI: perception
fusion tolerates a stale or dead companion (`ai_loss` scenario: the picture
degrades to `TOF_ONLY` and the flight continues — companion loss never escalates
a failsafe), and avoidance clamps every published command to FC-owned limits as
the last step (`SAF-040`).

The companion computer itself (Raspberry Pi 5 + Hailo) is selected hardware with
no software in this repository: Phase 16's AI inference phase is gated on a
model that does not exist.
