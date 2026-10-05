"""Ground-station protocol decoders (Phase 20, COM-001..004).

Dependency-free (Python 3 standard library only) decoders for the exact byte
formats the flight controller emits (DEC-015):

  * MAVLink v2 subset: HEARTBEAT(0), SYS_STATUS(1), ATTITUDE(30),
    LOCAL_POSITION_NED(32) with the protocol CRC-16/MCRF4XX and per-message
    CRC_EXTRA registry.
  * Versioned binary status record: MAGIC 'T' | SCHEMA_VER | TYPE | LEN | SEQ |
    reserved | timestamp_us u64 | 52-byte payload | CRC16-CCITT.
  * ICD-02 companion frames: SYNC 0xA5 0x5A | LEN | TYPE | SEQ | PAYLOAD |
    CRC16-CCITT.

`decode_stream` resynchronizes across a byte stream: it scans for a plausible
frame start, validates it, and counts garbage/dropped bytes instead of
crashing, so a truncated or corrupted capture is still usable (COM-002
"degraded link tolerated").
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Any, Dict, Iterator, List, Optional, Tuple

# ---------------------------------------------------------------- CRC helpers

def _crc16_ccitt(data: bytes, seed: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xor)."""
    crc = seed
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


# MAVLink CRC-16/MCRF4XX, exactly as the protocol's C reference implements it:
# nibble table + tmp = data ^ crc_lo; tmp ^= tmp << 4;
# crc = (crc >> 8) ^ (tmp << 8) ^ table[tmp >> 4].
# (A byte-table rewrite is NOT bit-identical to this form — verified against
# firmware-generated frames; the first GS version used one and rejected every
# real MAVLink frame.)
_CRC_NIBBLE = [
    0x0000, 0x1081, 0x2102, 0x3183, 0x4204, 0x5285, 0x6306, 0x7387,
    0x8408, 0x9489, 0xA50A, 0xB58B, 0xC60C, 0xD68D, 0xE70E, 0xF78F,
]


def _crc_mavlink(data: bytes, seed: int) -> int:
    """MAVLink CRC-16/MCRF4XX (nibble form, identical to mavlink2.c)."""
    crc = seed
    for b in data:
        tmp = (b ^ (crc & 0xFF)) & 0xFF
        tmp = (tmp ^ ((tmp << 4) & 0xFF)) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ _CRC_NIBBLE[tmp >> 4]) & 0xFFFF
    return crc


# ------------------------------------------------------------------ MAVLink v2

MAVLINK_STX = 0xFD
MAVLINK_HEADER_LEN = 8

MAVLINK_MSG_HEARTBEAT = 0
MAVLINK_MSG_SYS_STATUS = 1
MAVLINK_MSG_ATTITUDE = 30
MAVLINK_MSG_LOCAL_POSITION_NED = 32

# msgid -> (payload length, crc_extra) exactly as registered in mavlink2.c
MAVLINK_REGISTRY: Dict[int, Tuple[int, int]] = {
    MAVLINK_MSG_HEARTBEAT: (9, 50),
    MAVLINK_MSG_SYS_STATUS: (31, 124),
    MAVLINK_MSG_ATTITUDE: (28, 39),
    MAVLINK_MSG_LOCAL_POSITION_NED: (28, 185),
}


@dataclass
class MavlinkMessage:
    msgid: int
    sysid: int
    compid: int
    seq: int
    payload: bytes

    def field(self, fmt: str, offset: int) -> Any:
        return struct.unpack_from(fmt, self.payload, offset)[0]


def decode_mavlink(buf: bytes, off: int) -> Tuple[Optional[MavlinkMessage], int]:
    """Try to decode one MAVLink v2 frame at `off`.

    Returns (message, consumed). On failure the message is None and consumed
    is the number of bytes that can be safely dropped (>= 1).
    """
    if off + MAVLINK_HEADER_LEN + 2 > len(buf):
        return None, 0
    if buf[off] != MAVLINK_STX:
        return None, 1
    plen = buf[off + 1]
    msgid = buf[off + 7]
    reg = MAVLINK_REGISTRY.get(msgid)
    if reg is None or reg[0] != plen:
        return None, 1
    total = MAVLINK_HEADER_LEN + plen + 2
    if off + total > len(buf):
        return None, 0
    crc = _crc_mavlink(buf[off + 1: off + MAVLINK_HEADER_LEN + plen], reg[1])
    lo = buf[off + MAVLINK_HEADER_LEN + plen]
    hi = buf[off + MAVLINK_HEADER_LEN + plen + 1]
    if crc != ((hi << 8) | lo):
        return None, 1
    return (
        MavlinkMessage(
            msgid=msgid,
            sysid=buf[off + 5],
            compid=buf[off + 6],
            seq=buf[off + 4],
            payload=buf[off + MAVLINK_HEADER_LEN: off + MAVLINK_HEADER_LEN + plen],
        ),
        total,
    )


# --------------------------------------------------- binary status record (v2)

TELEM_MAGIC = 0x54          # 'T'
TELEM_SCHEMA_VER = 2        # v2 (Phase 24) appended `action` at payload[45]
TELEM_REC_STATUS = 1
TELEM_STATUS_PAYLOAD = 53
TELEM_HDR = 6
TELEM_RECORD_LEN = TELEM_HDR + 8 + TELEM_STATUS_PAYLOAD + 2

MISSION_STATES = {
    0: "IDLE", 1: "TAKEOFF", 2: "WAYPOINT", 3: "HOLD", 4: "RTL",
    5: "LAND", 6: "DONE", 7: "ABORT",
}
FAILSAFE_STATES = {
    0: "NONE", 1: "RC_LOSS", 2: "BATTERY", 3: "GEOFENCE", 4: "IMU",
    5: "ESTIMATOR", 6: "COMPANION",
}
PERCEPTION_MODES = {0: "NONE", 1: "TOF_ONLY", 2: "AI_ONLY", 3: "FUSED"}
AVOIDANCE_MODES = {0: "NONE", 1: "SLOW", 2: "STOP", 3: "RETREAT"}  # 3 added Phase 21 (DEC-018)
# Phase 24 / DEC-021: the action is what the vehicle is actually DOING about the
# failure. Without it an operator reading "RC_LOSS" during an IMU failure would
# believe the vehicle was returning home while it was cutting its motors.
SAFETY_ACTIONS = {0: "NONE", 1: "HOLD", 2: "RTL", 3: "LAND", 4: "MOTOR_STOP"}


@dataclass
class StatusRecord:
    schema_ver: int
    seq: int
    timestamp_us: int
    attitude: Tuple[float, float, float, float]      # w,x,y,z
    vel_ned: Tuple[float, float, float]
    altitude_m: float
    min_cell_v: float
    current_a: float
    mission: str
    failsafe: str
    perception: str
    avoidance: str
    companion_healthy: bool
    action: str              # Phase 24 schema v2: SAFETY_ACTIONS


def decode_status_record(buf: bytes, off: int) -> Tuple[Optional[StatusRecord], int]:
    """Decode the versioned telemetry status record at `off` (None, 0 if the
    buffer is too short, (None, >=1) if the frame is invalid)."""
    if off + TELEM_RECORD_LEN > len(buf):
        return None, 0
    if buf[off] != TELEM_MAGIC:
        return None, 1
    ver = buf[off + 1]
    rec = buf[off + 2]
    plen = buf[off + 3]
    seq = buf[off + 4]
    if ver != TELEM_SCHEMA_VER or rec != TELEM_REC_STATUS or plen != TELEM_STATUS_PAYLOAD:
        return None, 1
    body = buf[off: off + TELEM_HDR + 8 + TELEM_STATUS_PAYLOAD]
    crc = _crc16_ccitt(body)
    lo = buf[off + TELEM_HDR + 8 + TELEM_STATUS_PAYLOAD]
    hi = buf[off + TELEM_HDR + 8 + TELEM_STATUS_PAYLOAD + 1]
    if crc != ((hi << 8) | lo):
        return None, 1
    ts = struct.unpack_from("<Q", buf, off + TELEM_HDR)[0]
    p = off + TELEM_HDR + 8
    q = struct.unpack_from("<4f", buf, p)
    v = struct.unpack_from("<3f", buf, p + 16)
    alt, cell, cur = struct.unpack_from("<3f", buf, p + 28)
    mode, fs, perc, avoid, comp = struct.unpack_from("<5B", buf, p + 40)
    act = buf[p + 45]        # v2 tail byte; v1 frames are refused above
    return (
        StatusRecord(
            schema_ver=ver,
            seq=seq,
            timestamp_us=ts,
            attitude=q,
            vel_ned=v,
            altitude_m=alt,
            min_cell_v=cell,
            current_a=cur,
            mission=MISSION_STATES.get(mode, f"UNKNOWN({mode})"),
            failsafe=FAILSAFE_STATES.get(fs, f"UNKNOWN({fs})"),
            perception=PERCEPTION_MODES.get(perc, f"UNKNOWN({perc})"),
            avoidance=AVOIDANCE_MODES.get(avoid, f"UNKNOWN({avoid})"),
            companion_healthy=bool(comp),
            action=SAFETY_ACTIONS.get(act, f"UNKNOWN({act})"),
        ),
        TELEM_RECORD_LEN,
    )


# ------------------------------------------------------------ ICD-02 companion

ICD02_SYNC = b"\xa5\x5a"
ICD02_HEADER_LEN = 5
ICD02_MAX_PAYLOAD = 256
ICD02_TYPES = {0x01: "HEARTBEAT", 0x02: "OBSTACLE_SET", 0x03: "HEALTH",
               0x10: "FC_STATE", 0x11: "FC_CONFIG_ACK"}
CL_AI_STATES = {0: "UNKNOWN", 1: "IDLE", 2: "RUNNING", 3: "ERROR"}


@dataclass
class CompanionFrame:
    type: int
    type_name: str
    seq: int
    payload: bytes


def decode_icd02(buf: bytes, off: int) -> Tuple[Optional[CompanionFrame], int]:
    if off + 3 > len(buf):
        return None, 0
    if buf[off:off + 2] != ICD02_SYNC:
        return None, 1
    plen = buf[off + 2]
    if plen > ICD02_MAX_PAYLOAD:
        return None, 1
    total = ICD02_HEADER_LEN + plen + 2
    if off + total > len(buf):
        return None, 0
    crc = _crc16_ccitt(buf[off + 2: off + 2 + plen + 3])
    lo = buf[off + ICD02_HEADER_LEN + plen]
    hi = buf[off + ICD02_HEADER_LEN + plen + 1]
    if crc != ((hi << 8) | lo):
        return None, 1
    ftype = buf[off + 3]
    return (
        CompanionFrame(
            type=ftype,
            type_name=ICD02_TYPES.get(ftype, f"UNKNOWN(0x{ftype:02x})"),
            seq=buf[off + 4],
            payload=buf[off + ICD02_HEADER_LEN: off + ICD02_HEADER_LEN + plen],
        ),
        total,
    )


def decode_obstacle_set(payload: bytes) -> List[Dict[str, Any]]:
    """OBSTACLE_SET payload v1 -> list of detections (body FLU metres)."""
    if not payload:
        return []
    n = payload[0]
    if n > 16 or len(payload) < 1 + 3 * n + 12 * n:
        return []
    dets: List[Dict[str, Any]] = []
    base = 1 + 3 * n
    for i in range(n):
        vals = struct.unpack_from("<6h", payload, base + i * 12)
        dets.append(
            {
                "pos_m": [vals[0] / 100.0, vals[1] / 100.0, vals[2] / 100.0],
                "vel_m_s": [vals[3] / 100.0, vals[4] / 100.0, vals[5] / 100.0],
                "radius_m": payload[1 + i] / 100.0,
                "confidence": payload[1 + n + i] / 100.0,
                "class_id": payload[1 + 2 * n + i],
            }
        )
    return dets


# ---------------------------------------------------------------- stream decode

@dataclass
class StreamStats:
    mavlink_frames: int = 0
    status_records: int = 0
    companion_frames: int = 0
    crc_errors: int = 0
    garbage_bytes: int = 0
    unknown_msgids: List[int] = field(default_factory=list)
    seq_gaps: int = 0
    last_mav_seq: Optional[int] = None


def decode_stream(data: bytes) -> Tuple[List[Any], StreamStats]:
    """Decode a mixed GS byte log (MAVLink + status records + ICD-02 frames).

    Returns (events, stats). Garbage and CRC failures are counted, never fatal
    (COM-002 degraded-link tolerance).
    """
    events: List[Any] = []
    stats = StreamStats()
    off = 0
    n = len(data)
    while off < n:
        b = data[off]
        if b == MAVLINK_STX:
            msg, used = decode_mavlink(data, off)
            if msg is None:
                if used == 0:
                    break            # truncated tail: stop cleanly
                stats.garbage_bytes += used
                stats.crc_errors += 1
                off += used
                continue
            stats.mavlink_frames += 1
            if stats.last_mav_seq is not None:
                gap = (msg.seq - stats.last_mav_seq - 1) & 0xFF
                if gap and gap < 128:
                    stats.seq_gaps += gap
            stats.last_mav_seq = msg.seq
            events.append(msg)
            off += used
            continue
        if b == 0x54:                # status record magic 'T'
            rec, used = decode_status_record(data, off)
            if rec is None:
                if used == 0:
                    off += 1
                    continue
                stats.garbage_bytes += used
                stats.crc_errors += 1
                off += used
                continue
            stats.status_records += 1
            events.append(rec)
            off += used
            continue
        if data[off:off + 2] == ICD02_SYNC:
            fr, used = decode_icd02(data, off)
            if fr is not None:
                stats.companion_frames += 1
                events.append(fr)
                off += used
                continue
            if used == 0:
                off += 1
                continue
            stats.garbage_bytes += used
            off += used
            continue
        stats.garbage_bytes += 1
        off += 1
    return events, stats
