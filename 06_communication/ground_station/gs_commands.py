"""Ground-station command validation (Phase 20, COM-004, ICD-03).

The GS may REQUEST bounded things; it may never ARM and may never clear an
arming gate. Every command is validated here before it would be encoded, so a
malformed or unsafe request is rejected with an explicit reason and never
leaves the ground station.

Hard rules (COM-004):
  * ARM / DISARM are rejected unconditionally: arming is an RC-level gate
    (SAF-005); the FC ignores GS arming regardless of what we send.
  * Mode requests are limited to the RC-safe set (HOLD, RTL, LAND); mission
    execution and STABILIZE remain RC-selected.
  * Parameters must exist in the whitelist and be inside the FC-owned range.
  * Missions must respect the FC geofence/altitude envelope and the 16-entry
    waypoint cap.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

# FC-owned envelopes (mirror mission_sm.c defaults; verified in tests)
MAX_WAYPOINTS = 16
GEOFENCE_RADIUS_M = 50.0
MAX_WAYPOINT_ALT_M = 30.0
ARMING_ALLOWED = False           # COM-004: never, from any client

ALLOWED_COMMANDS = {
    "PING", "REQUEST_TELEMETRY", "SET_MODE", "REQUEST_RTL", "REQUEST_LAND",
    "SET_WAYPOINTS", "CLEAR_WAYPOINTS", "SET_PARAM", "SAVE_PARAMS",
    # GS-003: an emergency stop must be available from the GS. It is the SAME
    # bounded FC action as REQUEST_LAND (controlled descent, then motor stop by
    # the FC's own landing state machine) - the GS never gets to command motors,
    # and it still cannot arm or override a failsafe.
    "EMERGENCY_STOP",
}
ALLOWED_MODES = {"HOLD", "RTL", "LAND"}          # RC keeps arm/mode authority
FORBIDDEN_COMMANDS = {"ARM", "DISARM", "FORCE_ARM", "CLEAR_ARM_GATE", "OVERRIDE_FAILSAFE"}

# parameter whitelist: name -> (min, max, unit)
PARAM_BOUNDS: Dict[str, Tuple[float, float, str]] = {
    "GEOFENCE_RADIUS_M": (5.0, 200.0, "m"),
    "GEOFENCE_ALT_M": (5.0, 120.0, "m"),
    "RTL_ALT_M": (1.0, 100.0, "m"),
    "TAKEOFF_ALT_M": (0.5, 30.0, "m"),
    "AVOID_V_FWD_MAX": (0.2, 4.0, "m/s"),
    "AVOID_V_LAT_MAX": (0.1, 2.0, "m/s"),
    "RC_TIMEOUT_MS": (200, 2000, "ms"),
}


@dataclass
class ValidationResult:
    accepted: bool
    reason: str
    encoded: Optional[Dict[str, Any]] = None

    def __bool__(self) -> bool:      # pragma: no cover - convenience
        return self.accepted


def _reject(reason: str) -> ValidationResult:
    return ValidationResult(False, reason)


def validate_command(cmd: str, args: Optional[Dict[str, Any]] = None) -> ValidationResult:
    """Validate one GS command. Never raises; returns an explicit result."""
    if not isinstance(cmd, str) or not cmd:
        return _reject("empty command")
    name = cmd.strip().upper()
    args = args or {}

    if name in FORBIDDEN_COMMANDS:
        return _reject(f"'{name}' forbidden: arming and failsafe gates are FC/RC-owned (COM-004)")
    if name not in ALLOWED_COMMANDS:
        return _reject(f"unknown command '{name}'")

    if name == "PING":
        return ValidationResult(True, "ok", {"cmd": "PING"})
    if name == "EMERGENCY_STOP":
        # Always accepted, never parameterised: GS-003 requires the stop to be
        # available unconditionally (no mode gate, no confirmation prompt that
        # could leave the operator without a stop).
        return ValidationResult(True, "ok", {"cmd": name, "action": "LAND",
                                             "confirmed": True})
    if name == "REQUEST_TELEMETRY":
        rate = int(args.get("rate_hz", 10))
        if not 1 <= rate <= 50:
            return _reject(f"telemetry rate {rate} Hz outside 1..50")
        return ValidationResult(True, "ok", {"cmd": name, "rate_hz": rate})
    if name in ("SET_MODE", "REQUEST_RTL", "REQUEST_LAND"):
        mode = args.get("mode") or ("RTL" if name == "REQUEST_RTL" else
                                    "LAND" if name == "REQUEST_LAND" else None)
        if mode is None:
            return _reject("mode required")
        mode = str(mode).upper()
        if mode not in ALLOWED_MODES:
            return _reject(f"mode '{mode}' not GS-requestable (allowed: {sorted(ALLOWED_MODES)})")
        return ValidationResult(True, "ok", {"cmd": name, "mode": mode})
    if name in ("SET_WAYPOINTS", "CLEAR_WAYPOINTS"):
        if name == "CLEAR_WAYPOINTS":
            return ValidationResult(True, "ok", {"cmd": name})
        wps = args.get("waypoints")
        if not isinstance(wps, list) or not wps:
            return _reject("waypoints must be a non-empty list")
        return validate_mission(wps, encoded_cmd=name)
    if name == "SET_PARAM":
        pname = args.get("name")
        if pname not in PARAM_BOUNDS:
            return _reject(f"parameter '{pname}' not in whitelist")
        lo, hi, unit = PARAM_BOUNDS[pname]
        try:
            val = float(args.get("value"))
        except (TypeError, ValueError):
            return _reject(f"parameter '{pname}' value not numeric")
        if not lo <= val <= hi:
            return _reject(f"parameter '{pname}'={val} outside FC range [{lo}, {hi}] {unit}")
        return ValidationResult(True, "ok", {"cmd": name, "name": pname, "value": val})
    if name == "SAVE_PARAMS":
        return ValidationResult(True, "ok", {"cmd": name})
    return _reject("unhandled command")   # pragma: no cover


def validate_mission(waypoints: List[Any], encoded_cmd: str = "SET_WAYPOINTS") -> ValidationResult:
    """Waypoint list validation against the FC geofence/altitude envelope."""
    if not isinstance(waypoints, list):
        return _reject("waypoints must be a list")
    if len(waypoints) > MAX_WAYPOINTS:
        return _reject(f"{len(waypoints)} waypoints exceeds FC cap {MAX_WAYPOINTS}")
    clean: List[Dict[str, float]] = []
    for i, wp in enumerate(waypoints):
        if not isinstance(wp, dict):
            return _reject(f"waypoint {i} must be an object")
        try:
            x = float(wp.get("x", wp.get("lat", 0.0)))
            y = float(wp.get("y", wp.get("lon", 0.0)))
            z = float(wp.get("z", wp.get("alt", 0.0)))
        except (TypeError, ValueError):
            return _reject(f"waypoint {i} has non-numeric coordinates")
        if (x * x + y * y) ** 0.5 > GEOFENCE_RADIUS_M:
            return _reject(f"waypoint {i} outside geofence radius {GEOFENCE_RADIUS_M} m")
        if not 0.0 <= z <= MAX_WAYPOINT_ALT_M:
            return _reject(f"waypoint {i} altitude {z} m outside 0..{MAX_WAYPOINT_ALT_M} m")
        clean.append({"x": x, "y": y, "z": z})
    return ValidationResult(True, "ok", {"cmd": encoded_cmd, "waypoints": clean})
