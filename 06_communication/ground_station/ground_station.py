#!/usr/bin/env python3
"""Ground station (Phase 20, COM-001..004).

Host-side, standard library only. Capabilities:
  * decode an FC ground-station byte log (MAVLink v2 subset + versioned status
    record + ICD-02 companion frames), tolerating truncation/garbage;
  * show vehicle health, mission state, failsafe, battery, perception and
    avoidance status, with warning/critical classification;
  * manage waypoint missions with FC-envelope validation;
  * validate commands against the COM-004 rules (never arm);
  * `--self-test` runs the built-in test suite (used by the regression runner).

Usage:
  ground_station.py --from-log FILE [--json] [--strict]
  ground_station.py --mission FILE [--set-waypoints ...]
  ground_station.py --self-test
  ground_station.py --serial PORT      # H-gated: no rig present in this workspace

Exit codes: 0 ok, 1 validation/self-test failure or (with --strict) a CRITICAL
state was observed, 2 usage/IO error.
"""

from __future__ import annotations

import argparse
import json
import sys
import unittest
from dataclasses import dataclass
from typing import Any, Dict, List, Optional

from gs_commands import (  # noqa: E402  (local module import)
    ALLOWED_MODES,
    PARAM_BOUNDS,
    validate_command,
    validate_mission,
)
from gs_protocol import (  # noqa: E402
    MavlinkMessage,
    StatusRecord,
    StreamStats,
    decode_obstacle_set,
    decode_stream,
    _crc16_ccitt,
    _crc_mavlink,
    MAVLINK_REGISTRY,
    MAVLINK_STX,
    AVOIDANCE_MODES,
    SAFETY_ACTIONS,
    TELEM_STATUS_PAYLOAD,
)

WARNING_THRESHOLDS = {
    "cell_rtl_v": 3.4,     # SAF-030 RTL band
    "cell_warn_v": 3.5,    # SAF-030 warning
    "altitude_m": 30.0,    # geofence altitude
}


@dataclass
class Warning_:
    level: str          # "INFO" | "WARN" | "CRITICAL"
    code: str
    text: str


def classify(rec: StatusRecord) -> List[Warning_]:
    """Turn one status record into warnings (COM-004: safety state visible)."""
    out: List[Warning_] = []
    if rec.failsafe != "NONE":
        out.append(Warning_("CRITICAL", "FAILSAFE", f"failsafe active: {rec.failsafe}"))
    # Phase 24 / SAF-002: a motor stop is terminal and is called out on its own so
    # an operator never reads "RTL" when the vehicle is actually falling.
    if rec.action == "MOTOR_STOP":
        out.append(Warning_("CRITICAL", "SAFETY_ACTION",
                            "safety action: MOTOR STOP - vehicle is not flying"))
    elif rec.action in ("RTL", "LAND", "HOLD"):
        out.append(Warning_("WARN", "SAFETY_ACTION",
                            f"safety action: {rec.action}"))
    if rec.mission == "ABORT":
        out.append(Warning_("CRITICAL", "MISSION_ABORT", "mission aborted"))
    if rec.min_cell_v and rec.min_cell_v < WARNING_THRESHOLDS["cell_rtl_v"]:
        out.append(Warning_("CRITICAL", "BATT_RTL",
                            f"battery {rec.min_cell_v:.2f} V/cell below RTL band"))
    elif rec.min_cell_v and rec.min_cell_v < WARNING_THRESHOLDS["cell_warn_v"]:
        out.append(Warning_("WARN", "BATT_WARN", f"battery {rec.min_cell_v:.2f} V/cell"))
    if rec.altitude_m > WARNING_THRESHOLDS["altitude_m"]:
        out.append(Warning_("CRITICAL", "ALT_GEOFENCE", f"altitude {rec.altitude_m:.1f} m"))
    if not rec.companion_healthy:
        out.append(Warning_("WARN", "COMPANION", "companion link unhealthy (advisory)"))
    if rec.perception == "NONE":
        out.append(Warning_("WARN", "PERCEPTION", "perception picture invalid"))
    if rec.avoidance == "STOP":
        out.append(Warning_("WARN", "AVOID_STOP", "avoidance: hard stop commanded"))
    elif rec.avoidance == "SLOW":
        out.append(Warning_("INFO", "AVOID_SLOW", "avoidance: speed reduced"))
    return out


def summarize(events: List[Any], stats: StreamStats) -> Dict[str, Any]:
    records = [e for e in events if isinstance(e, StatusRecord)]
    mav = [e for e in events if isinstance(e, MavlinkMessage)]
    last = records[-1] if records else None
    warns: List[Warning_] = []
    for rec in records:
        warns.extend(classify(rec))
    # collapse duplicates while preserving first-seen order
    seen, unique = set(), []
    for w in warns:
        key = (w.level, w.code)
        if key not in seen:
            seen.add(key)
            unique.append({"level": w.level, "code": w.code, "text": w.text})
    return {
        "status_records": len(records),
        "mavlink_frames": len(mav),
        "companion_frames": stats.companion_frames,
        "crc_errors": stats.crc_errors,
        "garbage_bytes": stats.garbage_bytes,
        "seq_gaps": stats.seq_gaps,
        "last": None if last is None else {
            "t_s": last.timestamp_us / 1e6,
            "mission": last.mission,
            "failsafe": last.failsafe,
            "action": last.action,
            "perception": last.perception,
            "avoidance": last.avoidance,
            "altitude_m": round(last.altitude_m, 3),
            "min_cell_v": round(last.min_cell_v, 3),
            "current_a": round(last.current_a, 2),
            "attitude_wxyz": [round(v, 4) for v in last.attitude],
            "companion_healthy": last.companion_healthy,
        },
        "warnings": unique,
        "critical": any(w["level"] == "CRITICAL" for w in unique),
    }


def load_mission(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as fh:
        mission = json.load(fh)
    res = validate_mission(mission.get("waypoints", []))
    if not res.accepted:
        raise ValueError(f"mission rejected: {res.reason}")
    return mission


def save_mission(path: str, waypoints: List[Dict[str, float]]) -> Dict[str, Any]:
    res = validate_mission(waypoints)
    if not res.accepted:
        raise ValueError(f"mission rejected: {res.reason}")
    mission = {"version": 1, "waypoints": res.encoded["waypoints"]}
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(mission, fh, indent=2)
    return mission


# --------------------------------------------------------------- built-in tests

def _build_status_record(seq: int = 1, ts: int = 1000, alt: float = 1.5,
                         cell: float = 3.8, failsafe: int = 0,
                         mission: int = 3, comp: int = 1, avoid: int = 0,
                         action: int = 0) -> bytes:
    """Reference encoder mirroring telemetry.c (schema v2) so the GS self-test
    does not depend on the firmware binary."""
    payload = bytearray(53)
    struct_pack_into = __import__("struct").pack_into
    struct_pack_into("<4f", payload, 0, 1.0, 0.0, 0.0, 0.0)
    struct_pack_into("<3f", payload, 16, 0.0, 0.0, -0.1)
    struct_pack_into("<3f", payload, 28, alt, cell, 10.0)
    payload[40] = mission
    payload[41] = failsafe
    payload[42] = 3
    payload[43] = avoid
    payload[44] = comp
    payload[45] = action
    head = bytes([0x54, 2, 1, 53, seq, 0]) + ts.to_bytes(8, "little")
    body = head + bytes(payload)
    crc = _crc16_ccitt(body)
    return body + bytes([crc & 0xFF, crc >> 8])


def _build_mavlink(msgid: int, seq: int, payload: bytes) -> bytes:
    plen, extra = MAVLINK_REGISTRY[msgid]
    assert len(payload) == plen
    head = bytes([MAVLINK_STX, plen, 0, 0, seq, 1, 1, msgid])
    body = head + payload
    crc = _crc_mavlink(body[1:], extra)
    return body + bytes([crc & 0xFF, crc >> 8])


class ProtocolTests(unittest.TestCase):
    def test_status_record_roundtrip(self):
        events, stats = decode_stream(_build_status_record())
        self.assertEqual(stats.status_records, 1)
        rec = events[0]
        self.assertAlmostEqual(rec.altitude_m, 1.5, places=5)
        self.assertEqual(rec.mission, "HOLD")
        self.assertEqual(rec.failsafe, "NONE")
        self.assertTrue(rec.companion_healthy)
        self.assertEqual(rec.action, "NONE")

    def test_status_record_crc_error_is_counted(self):
        raw = bytearray(_build_status_record())
        raw[-1] ^= 0xFF
        events, stats = decode_stream(bytes(raw))
        self.assertEqual(stats.status_records, 0)
        self.assertGreaterEqual(stats.crc_errors, 1)

    def test_status_record_version_mismatch_rejected(self):
        raw = bytearray(_build_status_record())
        raw[1] = 9
        events, stats = decode_stream(bytes(raw))
        self.assertEqual(stats.status_records, 0)

    def test_mavlink_roundtrip(self):
        hb = _build_mavlink(0, 7, bytes(9))
        events, stats = decode_stream(hb)
        self.assertEqual(stats.mavlink_frames, 1)
        self.assertEqual(events[0].msgid, 0)
        self.assertEqual(events[0].seq, 7)

    def test_mavlink_crc_error_resync(self):
        raw = bytearray(_build_mavlink(0, 1, bytes(9)))
        raw[3] ^= 0xFF
        events, stats = decode_stream(bytes(raw))
        self.assertEqual(stats.mavlink_frames, 0)
        self.assertGreaterEqual(stats.crc_errors, 1)

    def test_mixed_stream_and_garbage(self):
        data = b"\x00\x11" + _build_mavlink(1, 2, bytes(31)) + b"\xff" + _build_status_record(seq=3)
        events, stats = decode_stream(data)
        self.assertEqual(stats.mavlink_frames, 1)
        self.assertEqual(stats.status_records, 1)
        self.assertGreaterEqual(stats.garbage_bytes, 3)

    def test_truncated_stream_does_not_crash(self):
        for cut in range(1, 20):
            events, stats = decode_stream(_build_mavlink(0, 0, bytes(9))[:cut])
            self.assertIsInstance(stats.status_records, int)

    def test_warning_classification(self):
        rec = decode_stream(_build_status_record(failsafe=1))[0][0]
        levels = {w.code for w in classify(rec)}
        self.assertIn("FAILSAFE", levels)
        low = decode_stream(_build_status_record(cell=3.2))[0][0]
        self.assertIn("BATT_RTL", {w.code for w in classify(low)})
        nocomp = decode_stream(_build_status_record(comp=0))[0][0]
        self.assertIn("COMPANION", {w.code for w in classify(nocomp)})

    def test_avoidance_retreat_mode_is_known(self):
        """Phase 21 added AVOID_RETREAT (mode 3, DEC-018). A GS that rendered
        it as UNKNOWN(3) would hide a real avoidance state from the operator."""
        self.assertEqual(AVOIDANCE_MODES.get(3), "RETREAT")
        rec = decode_stream(_build_status_record(avoid=3))[0][0]
        self.assertEqual(rec.avoidance, "RETREAT")
        self.assertNotIn("UNKNOWN", rec.avoidance)

    def test_safety_action_on_the_wire(self):
        """Phase 24 / SAF-002 / DEC-021: telemetry schema v2 carries the action
        the vehicle is actually taking, so a GS cannot render a motor stop as an
        RTL, and a v1-length frame is refused rather than mis-decoded."""
        self.assertEqual(TELEM_STATUS_PAYLOAD, 53)
        for code, name in SAFETY_ACTIONS.items():
            rec = decode_stream(_build_status_record(failsafe=4, action=code))[0][0]
            self.assertEqual(rec.failsafe, "IMU")
            self.assertEqual(rec.action, name)
        # the cross-domain case the schema bump exists for: IMU failure means the
        # vehicle cannot fly home, so the action must not read as RTL.
        stop = decode_stream(_build_status_record(failsafe=4, action=4))[0][0]
        codes = {w.code for w in classify(stop)}
        self.assertIn("SAFETY_ACTION", codes)
        rtl = decode_stream(_build_status_record(failsafe=1, action=2))[0][0]
        self.assertEqual(rtl.action, "RTL")
        self.assertNotEqual(rtl.action, stop.action)
        # unknown action codes are surfaced, never silently dropped to NONE
        self.assertEqual(
            decode_stream(_build_status_record(action=77))[0][0].action, "UNKNOWN(77)")

    def test_status_record_v1_length_is_refused(self):
        """A 52-byte v1 payload must not decode as a valid v2 record: a
        mis-decoded record is worse than a rejected frame."""
        raw = bytearray(_build_status_record())
        raw[3] = 52                      # plen back to the v1 size
        events, stats = decode_stream(bytes(raw))
        self.assertEqual(stats.status_records, 0)
        self.assertEqual(len(events), 0)

    def test_obstacle_set_decode(self):
        # count=1, radius 25 cm, conf 90%, class 1, pos (600,0,0) cm
        pl = bytearray([1, 25, 90, 1])
        for v in (600, 0, 0, -50, 0, 0):
            pl += v.to_bytes(2, "little", signed=True)
        dets = decode_obstacle_set(bytes(pl))
        self.assertEqual(len(dets), 1)
        self.assertAlmostEqual(dets[0]["pos_m"][0], 6.0, places=3)
        self.assertAlmostEqual(dets[0]["confidence"], 0.90, places=3)
        self.assertEqual(dets[0]["class_id"], 1)


class CommandTests(unittest.TestCase):
    def test_arm_is_always_rejected(self):
        for cmd in ("ARM", "arm", "DISARM", "FORCE_ARM", "CLEAR_ARM_GATE", "OVERRIDE_FAILSAFE"):
            res = validate_command(cmd)
            self.assertFalse(res.accepted, cmd)
            self.assertIn("COM-004", res.reason)

    def test_allowed_commands(self):
        self.assertTrue(validate_command("PING").accepted)
        self.assertTrue(validate_command("REQUEST_RTL").accepted)
        self.assertTrue(validate_command("SET_MODE", {"mode": "HOLD"}).accepted)
        self.assertFalse(validate_command("SET_MODE", {"mode": "STABILIZE"}).accepted)
        self.assertFalse(validate_command("NOPE").accepted)

    def test_parameter_bounds(self):
        ok = validate_command("SET_PARAM", {"name": "GEOFENCE_RADIUS_M", "value": 100})
        self.assertTrue(ok.accepted)
        bad = validate_command("SET_PARAM", {"name": "GEOFENCE_RADIUS_M", "value": 5000})
        self.assertFalse(bad.accepted)
        self.assertFalse(validate_command("SET_PARAM", {"name": "HIDDEN", "value": 1}).accepted)

    def test_mission_validation(self):
        good = [{"x": 1.0, "y": 2.0, "z": 5.0}]
        self.assertTrue(validate_mission(good).accepted)
        self.assertFalse(validate_mission([{"x": 100.0, "y": 0.0, "z": 5.0}]).accepted)
        self.assertFalse(validate_mission([{"x": 0.0, "y": 0.0, "z": 60.0}]).accepted)
        self.assertFalse(validate_mission([{"x": 0.0, "y": 0.0, "z": 1.0}] * 17).accepted)
        self.assertFalse(validate_mission([{"x": "a", "y": 0, "z": 1}]).accepted)

    def test_emergency_stop_is_always_available(self):
        """GS-003: the stop exists, is unconditional, and stays inside the FC's
        own bounded action set (no motor command, no failsafe override)."""
        for args in ({}, {"confirm": False}, {"mode": "HOLD"}):
            res = validate_command("EMERGENCY_STOP", args)
            self.assertTrue(res.accepted, res.reason)
            self.assertEqual(res.encoded["action"], "LAND")
            self.assertTrue(res.encoded["confirmed"])
        # it is a stop, not an arming or override path
        self.assertFalse(validate_command("EMERGENCY_STOP_ARM").accepted)
        for forbidden in ("OVERRIDE_FAILSAFE", "CLEAR_ARM_GATE"):
            self.assertFalse(validate_command(forbidden).accepted)

    def test_telemetry_rate_bounds(self):
        self.assertTrue(validate_command("REQUEST_TELEMETRY", {"rate_hz": 20}).accepted)
        self.assertFalse(validate_command("REQUEST_TELEMETRY", {"rate_hz": 500}).accepted)


def self_test() -> int:
    suite = unittest.TestLoader().loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    return 0 if result.wasSuccessful() else 1


# --------------------------------------------------------------------- CLI

def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Autonomous drone ground station")
    ap.add_argument("--from-log", metavar="FILE", help="decode an FC GS byte log")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--strict", action="store_true", help="exit 1 if a CRITICAL state is seen")
    ap.add_argument("--mission", metavar="FILE", help="validate/load a waypoint mission JSON")
    ap.add_argument("--set-waypoints", metavar="JSON", help="save a mission file from JSON")
    ap.add_argument("--self-test", action="store_true", help="run built-in tests")
    ap.add_argument("--serial", metavar="PORT", help="H-gated: serial capture (no rig here)")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()

    if args.serial:
        print(f"SKIP: serial capture on {args.serial} is H-gated "
              "(no rig/board present; see HIL_DESIGN.md)", file=sys.stderr)
        return 0

    if args.set_waypoints:
        try:
            wps = json.loads(args.set_waypoints)
            mission = save_mission(args.mission or "mission.json", wps)
        except (ValueError, OSError) as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2
        print(json.dumps(mission, indent=2))
        return 0

    if args.mission:
        try:
            mission = load_mission(args.mission)
        except (ValueError, OSError) as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2
        print(f"mission OK: {len(mission['waypoints'])} waypoints within FC envelope")
        return 0

    if not args.from_log:
        ap.print_usage()
        print("nothing to do: pass --from-log, --mission, --set-waypoints or --self-test",
              file=sys.stderr)
        return 2

    try:
        with open(args.from_log, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    events, stats = decode_stream(data)
    summary = summarize(events, stats)
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        last = summary["last"]
        print(f"decoded {summary['status_records']} status records, "
              f"{summary['mavlink_frames']} MAVLink frames, "
              f"{summary['companion_frames']} companion frames "
              f"(crc_errors={summary['crc_errors']}, garbage={summary['garbage_bytes']}, "
              f"seq_gaps={summary['seq_gaps']})")
        if last:
            print(f"last: t={last['t_s']:.2f}s mission={last['mission']} "
                  f"failsafe={last['failsafe']} action={last['action']} "
                  f"perception={last['perception']} "
                  f"avoidance={last['avoidance']} alt={last['altitude_m']:.2f} m "
                  f"batt={last['min_cell_v']:.2f} V/cell "
                  f"companion={'ok' if last['companion_healthy'] else 'DOWN'}")
        for w in summary["warnings"]:
            print(f"{w['level']:<8} {w['code']:<16} {w['text']}")
    if args.strict and summary["critical"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
