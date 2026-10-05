#!/usr/bin/env python3
"""DShot600 capture analyser (Phase 22, HW-006, HIL-5).

Decodes the capture written by the HIL rig (``hil_rig --capture FILE``): the
exact 16-bit words an ESC would receive, one record per motor per control tick:

    offset  size  field
    0       8     t_us     little-endian, rig clock (us)
    8       2     frame    DShot600 word (CRC-4 included)
    10      1     armed    1 = motors armed at this tick
    11      1     crc_err  1 = the rig flagged a decode failure

The CRC-4 here is an INDEPENDENT implementation of the BLHeli/Bluejay
convention (esc_dshot.c is the firmware side). That independence is the point:
if both implementations agree on every captured frame, the bit stream the FC
emitted is well formed - the software equivalent of comparing an ESC analyser
against the wire (HIL-5). It is NOT evidence about a real ESC.

Usage:  dshot_analyzer.py CAPTURE [--expect-ticks N] [--json]
Exit:   0 = all checks passed, 1 = a check failed, 2 = usage/file error.
"""

import json
import struct
import sys

REC = struct.Struct("<QHBb")   # t_us, frame, armed, crc_err
DSHOT_STOP = 0
DSHOT_MIN_THROTTLE = 48
DSHOT_MAX_THROTTLE = 2047


def crc4(payload12: int) -> int:
    """CRC-4 over the 12-bit payload: (v ^ (v>>4) ^ (v>>8)) & 0xF (BLHeli/Bluejay)."""
    v = payload12 & 0x0FFF
    return (v ^ (v >> 4) ^ (v >> 8)) & 0x0F


def decode(frame: int):
    """-> (throttle, telemetry_request, crc_ok) or None when the CRC fails."""
    payload = (frame >> 4) & 0x0FFF
    if crc4(payload) != (frame & 0x0F):
        return None
    return (payload >> 1) & 0x07FF, bool(payload & 0x01), True


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[1]
    expect_ticks = None
    want_json = "--json" in argv
    for i, a in enumerate(argv):
        if a == "--expect-ticks" and i + 1 < len(argv):
            expect_ticks = int(argv[i + 1], 0)

    with open(path, "rb") as fh:
        blob = fh.read()

    if len(blob) % REC.size != 0:
        print("FAIL capture length %d is not a multiple of the %d-byte record"
              % (len(blob), REC.size))
        return 1

    records = REC.iter_unpack(blob)
    stats = {
        "records": 0, "crc_ok": 0, "crc_bad": 0, "disarm_not_stop": 0,
        "armed_stop": 0, "out_of_range": 0, "tick_mismatch": 0,
        "period_mismatch": 0, "max_throttle": 0, "min_armed_throttle": 0xFFFF,
        "ticks": 0, "first_t_us": None, "last_t_us": None,
        "distinct_periods": 0, "armed_from_us": None,
    }
    failures = []
    cur_tick = None
    cur_armed = 0
    per_tick = 0
    prev_t = None
    periods = {}

    for t_us, frame, armed, crc_err in records:
        stats["records"] += 1
        if stats["first_t_us"] is None:
            stats["first_t_us"] = t_us
        stats["last_t_us"] = t_us

        if crc_err:
            stats["crc_bad"] += 1

        dec = decode(frame)
        if dec is None:
            stats["crc_bad"] += 1
            if len(failures) < 5:
                failures.append("frame 0x%04X at t=%d us fails CRC-4" % (frame, t_us))
            continue
        thr, _tel, _ok = dec
        stats["crc_ok"] += 1
        if thr > DSHOT_MAX_THROTTLE:
            stats["out_of_range"] += 1
            if len(failures) < 5:
                failures.append("throttle %d at t=%d us exceeds %d"
                                % (thr, t_us, DSHOT_MAX_THROTTLE))

        # tick structure: 4 motor frames per control tick, same timestamp
        if t_us != cur_tick:
            if cur_tick is not None:
                if per_tick != 4:
                    stats["tick_mismatch"] += 1
                    if len(failures) < 5:
                        failures.append("tick %d carried %d motor frames (expected 4)"
                                        % (cur_tick, per_tick))
                if prev_t is not None:
                    periods[(t_us - prev_t)] = periods.get(t_us - prev_t, 0) + 1
                prev_t = t_us
            cur_tick = t_us
            per_tick = 0
            stats["ticks"] += 1
        per_tick += 1

        if armed:
            if stats["armed_from_us"] is None:
                stats["armed_from_us"] = t_us
            stats["armed_stop"] += 0 if thr == DSHOT_STOP else 1
            if thr > stats["max_throttle"]:
                stats["max_throttle"] = thr
            if thr > DSHOT_MIN_THROTTLE and thr < stats["min_armed_throttle"]:
                stats["min_armed_throttle"] = thr
        else:
            # a disarmed vehicle must emit STOP on every motor (CTRL-005)
            if thr != DSHOT_STOP:
                stats["disarm_not_stop"] += 1
                if len(failures) < 5:
                    failures.append("disarmed motor emitted throttle %d at t=%d us"
                                    % (thr, t_us))
        cur_armed = armed

    if cur_tick is not None and per_tick != 4:
        stats["tick_mismatch"] += 1
        failures.append("final tick %d carried %d motor frames (expected 4)"
                        % (cur_tick, per_tick))

    stats["distinct_periods"] = len(periods)
    if len(periods) > 1:
        stats["period_mismatch"] = 1
        failures.append("frame groups are not evenly spaced: %s"
                        % sorted((p, c) for p, c in periods.items())[:5])
    elif periods:
        period = next(iter(periods))
        if period != 1000:
            failures.append("frame period is %d us, expected 1000 us (1 kHz tick)"
                            % period)

    if expect_ticks is not None and stats["ticks"] != expect_ticks:
        failures.append("expected %d ticks, capture has %d" % (expect_ticks, stats["ticks"]))

    if stats["crc_bad"]:
        failures.append("%d frame(s) failed CRC-4 (first: %s)"
                        % (stats["crc_bad"], failures[0] if failures else "?"))
    if stats["disarm_not_stop"]:
        failures.append("%d disarmed motor frame(s) were not STOP" % stats["disarm_not_stop"])
    if stats["armed_from_us"] is None:
        failures.append("no armed frame in the capture (the FC never armed)")
    elif stats["min_armed_throttle"] <= DSHOT_MIN_THROTTLE:
        failures.append("no armed frame above the throttle floor: the motors never ran")

    stats["period_us"] = next(iter(periods)) if len(periods) == 1 else None
    stats["ok"] = not failures

    if want_json:
        print(json.dumps({"stats": stats, "failures": failures}, indent=2))
    else:
        print("dshot: records=%d ticks=%d crc_ok=%d crc_bad=%d period_us=%s"
              % (stats["records"], stats["ticks"], stats["crc_ok"], stats["crc_bad"],
                 stats["period_us"]))
        print("dshot: armed_from_us=%s max_throttle=%d min_armed_throttle=%d "
              "disarm_violations=%d out_of_range=%d"
              % (stats["armed_from_us"], stats["max_throttle"],
                 stats["min_armed_throttle"], stats["disarm_not_stop"],
                 stats["out_of_range"]))
        for f in failures:
            print("FAIL %s" % f)
        print("dshot: %s" % ("PASS" if stats["ok"] else "FAIL"))

    return 0 if stats["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))