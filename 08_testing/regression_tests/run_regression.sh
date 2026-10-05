#!/usr/bin/env bash
# Regression runner (TEST-001..004, Phase 16 03_regression).
#
# Section numbers below are the step numbers printed in the summary:
#    1. build: fresh configure + ninja
#    2. unit + integration suite (fc_tests)           - safety-critical
#    3. determinism double-run diff (SIM-002)         - safety-critical
#    4. fault-scenario matrix with expected nav
#       sequences (SIM-004, SAF-001/003, NAV-*)
#    5. ground station self-test + end-to-end decode (COM-001..004)
#    6. host HIL: link, rates, DShot capture,
#       fault injection, watchdog interlock (SAF-004/TEST-002)
#    7. requirement + architecture audit (TEST-004, FW-001/004)
#    8. manufacturing release package: regenerate + audit (MFG-001)
#    9. H-gated hardware steps (TEST-002)             - SKIPPED with a clear
#       message when the board/toolchain is absent (never silently)
#   10. documentation audit: index, links, markers, facts (Phase 26)
#   11. V&V review gate: classify every requirement, resolve evidence refs,
#       require critical items verified or explicitly blocked (Phase 27)
#   12. release manifest audit: tree digest, item digests, gated absence,
#       OPS-001 row (Phase 28)
#   13. final A-Z audit: marker register, fixed/residual findings (Phase 29)
#
# Exit code: 0 only if every executed step passed. Failures are accumulated;
# the exit status of every command is preserved (no filters mask them).
set -u

FW="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../02_firmware" && pwd)"
# resolved before the cd below (BASH_SOURCE may be a relative path)
GS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../06_communication/ground_station" && pwd)"
HIL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../07_simulation/hardware_in_the_loop" && pwd)"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$FW" || exit 1

FAILURES=0
declare -a RESULTS

report() {  # report <PASS|FAIL|SKIP> <name> [detail]
    RESULTS+=("$1: $2${3:+ ($3)}")
    case "$1" in
        FAIL) FAILURES=$((FAILURES+1)) ;;
    esac
}

echo "=== Regression run $(date -u '+%Y-%m-%dT%H:%M:%SZ') ==="

# ---- 1. build (fresh configure keeps CMake cache issues from hiding errors) --
if cmake -S . -B build > /tmp/reg_cmake.log 2>&1; then
    report PASS "cmake configure"
else
    report FAIL "cmake configure" "$(tail -3 /tmp/reg_cmake.log)"
fi

BUILD_LOG=/tmp/reg_build.log
if cmake --build build -j 4 > "$BUILD_LOG" 2>&1; then
    report PASS "build (ninja)"
else
    report FAIL "build (ninja)" "$(grep -iE 'error' "$BUILD_LOG" | head -3)"
fi

# ---- 2. unit + integration suite -------------------------------------------
if [ -x build/tests/fc_tests.exe ]; then
    TEST_LOG=/tmp/reg_tests.log
    if ./build/tests/fc_tests.exe > "$TEST_LOG" 2>&1; then
        SUMMARY=$(grep -oE '[0-9]+/[0-9]+ passed' "$TEST_LOG" | tail -1)
        report PASS "unit+integration suite" "$SUMMARY"
    else
        report FAIL "unit+integration suite" "$(grep 'FAIL' "$TEST_LOG" | head -3)"
    fi
else
    report FAIL "unit+integration suite" "fc_tests.exe missing"
fi

# ---- 3. determinism (SIM-002): two identical runs must be byte-identical ----
DET_LOG=/tmp/reg_det.log
FC_SIM_FAST=1 FC_SIM_TICKS=8000 FC_SIM_SEED=1234 ./build/fc_sim.exe nominal > /tmp/reg_det_a.log 2>&1
A=$?
FC_SIM_FAST=1 FC_SIM_TICKS=8000 FC_SIM_SEED=1234 ./build/fc_sim.exe nominal > /tmp/reg_det_b.log 2>&1
B=$?
if [ "$A" -eq 0 ] && [ "$B" -eq 0 ] && diff -q /tmp/reg_det_a.log /tmp/reg_det_b.log > /dev/null; then
    report PASS "determinism double-run (SIM-002)" "diff = 0"
else
    report FAIL "determinism double-run (SIM-002)" "diff nonzero or exit $A/$B"
fi

# ---- 4. fault-scenario matrix with expected nav sequences (SIM-004) ---------
check_scenario() {  # <scenario> <ticks> <expected-subsequence-regex>
    local sc="$1" ticks="$2" expect="$3"
    local log=/tmp/reg_sc_$sc.log
    FC_SIM_FAST=1 FC_SIM_TICKS="$ticks" FC_SIM_SEED=42 ./build/fc_sim.exe "$sc" > "$log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        report FAIL "scenario $sc" "exit $rc"
        return
    fi
    local seq
    seq=$(grep 'nav:' "$log" | sed 's/nav: \([A-Z]*\).*/\1/' | tr '\n' ' ')
    if echo "$seq" | grep -qE "$expect"; then
        local misses
        misses=$(grep -oE 'deadline_misses=[0-9]+' "$log" | tail -1)
        if [ "$misses" = "deadline_misses=0" ]; then
            report PASS "scenario $sc" "$seq"
        else
            report FAIL "scenario $sc" "$misses"
        fi
    else
        report FAIL "scenario $sc" "got [$seq] expected [$expect]"
    fi
}

check_scenario nominal     30000 'TAKEOFF.*HOLD'
check_scenario rc_loss     30000 'HOLD.*(LAND|RTL).*DONE'
check_scenario imu_dropout 30000 'HOLD.*ABORT'
check_scenario battery_low 30000 'HOLD.*LAND.*DONE'
check_scenario gnss_loss   30000 'HOLD.*LAND.*DONE'
check_scenario noiseless   30000 'TAKEOFF.*HOLD'

# ai_loss: companion stream dies at 6 s. Phase 17 gives it a real FC-side
# consumer (perception fusion): the picture must degrade to FC-only ranging
# (TOF_ONLY, mode=1) and the flight must CONTINUE - perception is advisory
# only (DEC-007), so no failsafe, no abort.
AI_LOG=/tmp/reg_sc_ai_loss.log
FC_SIM_FAST=1 FC_SIM_TICKS=30000 FC_SIM_SEED=42 ./build/fc_sim.exe ai_loss > "$AI_LOG" 2>&1
AI_RC=$?
AI_SEQ=$(grep 'nav:' "$AI_LOG" | sed 's/nav: \([A-Z]*\).*/\1/' | tr '\n' ' ')
AI_PMODE=$(grep 'perception: mode=' "$AI_LOG" | tail -1)
if [ "$AI_RC" -eq 0 ] && echo "$AI_SEQ" | grep -qE 'TAKEOFF.*HOLD' \
   && echo "$AI_PMODE" | grep -qE 'mode=1 '; then
    report PASS "scenario ai_loss (advisory degradation)" "$AI_SEQ[$AI_PMODE]"
else
    report FAIL "scenario ai_loss (advisory degradation)" \
        "got [$AI_SEQ] [$AI_PMODE]"
fi

# imu_rc_loss (Phase 24, SAF-002, DEC-021): IMU dies at 6 s and RC at 6.2 s.
# The reported failsafe becomes RC_LOSS but the vehicle cannot fly, so the
# safety ACTION must be MOTOR_STOP - this is the cross-domain case where a GS
# that only renders the failsafe name would tell the operator it is returning
# home while it is actually falling. Asserted on the wire, not on a unit test.
IMURC_LOG=/tmp/reg_sc_imu_rc_loss.log
FC_SIM_FAST=1 FC_SIM_TICKS=30000 FC_SIM_SEED=42 ./build/fc_sim.exe imu_rc_loss \
    > "$IMURC_LOG" 2>&1
IMURC_RC=$?
IMURC_NAV=$(grep 'nav:' "$IMURC_LOG" | sed 's/nav: \([A-Z]*\).*/\1/' | tr '\n' ' ')
IMURC_SAFE=$(grep 'safety:' "$IMURC_LOG" | tail -1)
if [ "$IMURC_RC" -eq 0 ] && echo "$IMURC_NAV" | grep -qE 'TAKEOFF.*HOLD.*ABORT' \
   && echo "$IMURC_SAFE" | grep -qE 'failsafe=1 action=4' \
   && grep -q 'deadline_misses=0' "$IMURC_LOG"; then
    report PASS "scenario imu_rc_loss (safety action dominates)" \
        "$IMURC_NAV|$IMURC_SAFE"
else
    report FAIL "scenario imu_rc_loss (safety action dominates)" \
        "exit $IMURC_RC nav [$IMURC_NAV] safety [$IMURC_SAFE]"
fi

# ---- 5. ground station (Phase 20, COM-001..004) -----------------------------
# Self-test of the GS decoders/command validation, then a real end-to-end
# decode of firmware-generated byte logs: nominal must be clean (exit 0),
# battery_low must surface CRITICAL state (exit 1 under --strict).
PY=python
command -v "$PY" > /dev/null 2>&1 || PY=python3

if [ ! -x build/fc_sim.exe ]; then
    report FAIL "ground station" "fc_sim.exe missing"
elif ! command -v "$PY" > /dev/null 2>&1; then
    report SKIP "ground station" "H-gated-ish: no python interpreter on PATH"
else
    if (cd "$GS_DIR" && "$PY" ground_station.py --self-test) > /tmp/reg_gs_selftest.log 2>&1; then
        report PASS "ground station self-test" "$(grep -oE 'Ran [0-9]+ tests' /tmp/reg_gs_selftest.log | tail -1)"
    else
        report FAIL "ground station self-test" "$(tail -3 /tmp/reg_gs_selftest.log)"
    fi

    GS_LOG_N=/tmp/reg_gs_nominal.bin
    GS_LOG_B=/tmp/reg_gs_batt.bin
    FC_SIM_FAST=1 FC_SIM_TICKS=4000 FC_SIM_SEED=42 \
        FC_SIM_GS_LOG="$GS_LOG_N" ./build/fc_sim.exe nominal > /dev/null 2>&1
    N_RC=$?
    (cd "$GS_DIR" && "$PY" ground_station.py --from-log "$GS_LOG_N" --strict) \
        > /tmp/reg_gs_nominal.log 2>&1
    N_GS=$?
    FC_SIM_FAST=1 FC_SIM_TICKS=12000 FC_SIM_SEED=42 \
        FC_SIM_GS_LOG="$GS_LOG_B" ./build/fc_sim.exe battery_low > /dev/null 2>&1
    B_RC=$?
    (cd "$GS_DIR" && "$PY" ground_station.py --from-log "$GS_LOG_B" --strict) \
        > /tmp/reg_gs_batt.log 2>&1
    B_GS=$?

    if [ "$N_RC" -eq 0 ] && [ "$N_GS" -eq 0 ] && [ "$B_RC" -eq 0 ] && [ "$B_GS" -eq 1 ] \
       && grep -q "CRITICAL" /tmp/reg_gs_batt.log; then
        report PASS "ground station end-to-end decode" \
            "nominal clean; battery_low raised CRITICAL (gs exit 1)"
    else
        report FAIL "ground station end-to-end decode" \
            "sim $N_RC/$B_RC gs $N_GS/$B_GS (expected 0/1)"
    fi

    # Schema v2 (Phase 24): the GS must show the action, not just the failsafe,
    # and a motor stop must reach the operator as its own CRITICAL line. If the
    # firmware and the decoder ever drift apart again the v2 frames are simply
    # refused, so this step is the end-to-end guard on that contract.
    GS_LOG_S=/tmp/reg_gs_imurc.bin
    FC_SIM_FAST=1 FC_SIM_TICKS=12000 FC_SIM_SEED=42 \
        FC_SIM_GS_LOG="$GS_LOG_S" ./build/fc_sim.exe imu_rc_loss > /dev/null 2>&1
    S_RC=$?
    (cd "$GS_DIR" && "$PY" ground_station.py --from-log "$GS_LOG_S" --strict) \
        > /tmp/reg_gs_imurc.log 2>&1
    S_GS=$?
    if [ "$S_RC" -eq 0 ] && [ "$S_GS" -eq 1 ] \
       && grep -q "action=MOTOR_STOP" /tmp/reg_gs_imurc.log \
       && grep -q "SAFETY_ACTION" /tmp/reg_gs_imurc.log \
       && grep -q "status records, " /tmp/reg_gs_imurc.log \
       && grep -oE 'decoded [1-9][0-9]* status records' /tmp/reg_gs_imurc.log > /dev/null; then
        report PASS "ground station telemetry schema v2 (action on the wire)" \
            "$(grep -oE 'decoded [0-9]+ status records' /tmp/reg_gs_imurc.log | head -1)"
    else
        report FAIL "ground station telemetry schema v2 (action on the wire)" \
            "sim $S_RC gs $S_GS (expected 1); $(tail -2 /tmp/reg_gs_imurc.log | tr '\n' ' ')"
    fi
fi

# ---- 6. HIL host rig (Phase 22, DEC-019, HIL-0a..0e) ----------------------
# The HIL target is built first: same application, different HAL backend.
HIL_CFG=/tmp/reg_hil_cmake.log
HIL_BUILD=/tmp/reg_hil_build.log
if cmake -S . -B build-hil -DFC_TARGET=hil > "$HIL_CFG" 2>&1 \
   && cmake --build build-hil -j 4 > "$HIL_BUILD" 2>&1; then
    report PASS "HIL target build (FC_TARGET=hil)"
else
    report FAIL "HIL target build (FC_TARGET=hil)" \
        "$(grep -iE 'error' "$HIL_BUILD" | head -3)"
fi

# The real application runs behind a HAL backend that serves every sensor,
# actuator and storage access over a framed link to a host rig which owns the
# vehicle world. These steps are EXECUTED on the host: they are not hardware
# evidence (HIL-1..HIL-6 stay H-gated) but they are repeatable critical-
# interface evidence. Every exit status below is captured explicitly - no
# filters mask a command's status.

pick_port() {   # first bindable loopback port at or after $1
    if command -v "$PY" > /dev/null 2>&1; then
        "$PY" - "$1" <<'PYEOF' 2>/dev/null
import socket, sys
for p in range(int(sys.argv[1]), int(sys.argv[1]) + 200):
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", p))
    except OSError:
        s.close()
        continue
    s.close()
    print(p)
    break
PYEOF
        return 0
    fi
    echo "$1"
}

# run a rig + FC pair; sets HIL_FC_RC and the log paths
hil_run() {   # <name> <fc_ticks> [rig args...] -- [fc args...]
    local name="$1" ticks="$2"; shift 2
    local rig_args=() fc_args=()
    local seen_sep=0
    for a in "$@"; do
        if [ "$a" = "--" ]; then seen_sep=1; continue; fi
        if [ $seen_sep -eq 1 ]; then fc_args+=("$a"); else rig_args+=("$a"); fi
    done
    local port
    port="$(pick_port 45700)"
    HIL_FC_LOG="/tmp/reg_hil_${name}_fc.log"
    HIL_RIG_LOG="/tmp/reg_hil_${name}_rig.log"
    HIL_CAP="/tmp/reg_hil_${name}_dshot.bin"
    ./build-hil/hil_rig.exe --port "$port" --ticks "$ticks" \
        --capture "$HIL_CAP" "${rig_args[@]}" > "$HIL_RIG_LOG" 2>&1 &
    local rig_pid=$!
    sleep 1
    timeout 300 ./build-hil/fc_hil.exe --port "$port" --ticks "$ticks" \
        "${fc_args[@]}" > "$HIL_FC_LOG" 2>&1
    HIL_FC_RC=$?
    wait "$rig_pid" 2>/dev/null
    HIL_RIG_RC=$?
}

hil_metric() {   # <log> <grep-pattern> -> value or ""
    grep -oE "$2" "$1" 2>/dev/null | tail -1
}

if [ ! -x build-hil/fc_hil.exe ] || [ ! -x build-hil/hil_rig.exe ]; then
    report FAIL "HIL target build" "fc_hil.exe / hil_rig.exe missing"
else
    # -- 6a nominal loopback: link integrity, sensor-rate fidelity, mission ---
    hil_run nominal 6000
    fc_stalls="$(hil_metric "$HIL_FC_LOG" 'link_stalls=[0-9]+' | cut -d= -f2)"
    fc_crc="$(hil_metric "$HIL_FC_LOG" 'crc_errors=[0-9]+' | cut -d= -f2)"
    fc_gaps="$(hil_metric "$HIL_FC_LOG" 'seq_gaps=[0-9]+' | cut -d= -f2)"
    fc_drop="$(hil_metric "$HIL_FC_LOG" 'tx_drops=[0-9]+' | cut -d= -f2)"
    imu_age="$(hil_metric "$HIL_FC_LOG" 'sensor\[imu\] age_max_us=[0-9]+' | grep -oE '[0-9]+$')"
    gnss_age="$(hil_metric "$HIL_FC_LOG" 'sensor\[gnss\] age_max_us=[0-9]+' | grep -oE '[0-9]+$')"
    baro_age="$(hil_metric "$HIL_FC_LOG" 'sensor\[baro\] age_max_us=[0-9]+' | grep -oE '[0-9]+$')"
    rc_age="$(hil_metric "$HIL_FC_LOG" 'sensor\[rc\] age_max_us=[0-9]+' | grep -oE '[0-9]+$')"
    rig_crc="$(hil_metric "$HIL_RIG_LOG" 'crc_errors=[0-9]+' | head -1 | cut -d= -f2)"
    rig_gaps="$(hil_metric "$HIL_RIG_LOG" 'seq_gaps=[0-9]+' | head -1 | cut -d= -f2)"
    rig_dshot="$(hil_metric "$HIL_RIG_LOG" 'dshot_crc_err=[0-9]+' | cut -d= -f2)"
    nav_seq="$(grep 'nav:' "$HIL_FC_LOG" | sed 's/nav: \([A-Z]*\).*/\1/' | tr '\n' ' ')"
    if [ "$HIL_FC_RC" -eq 0 ] && [ "$HIL_RIG_RC" -eq 0 ] \
       && [ "${fc_stalls:-1}" -eq 0 ] && [ "${fc_crc:-1}" -eq 0 ] \
       && [ "${fc_gaps:-1}" -eq 0 ] && [ "${fc_drop:-1}" -eq 0 ] \
       && [ "${rig_crc:-1}" -eq 0 ] && [ "${rig_gaps:-1}" -eq 0 ] \
       && [ "${rig_dshot:-1}" -eq 0 ] \
       && [ "${imu_age:-1}" -le 1000 ] && [ "${gnss_age:-1}" -ge 90000 ] \
       && [ "${baro_age:-1}" -ge 18000 ] && [ "${rc_age:-1}" -le 10000 ] \
       && echo "$nav_seq" | grep -qE 'TAKEOFF.*HOLD'; then
        report PASS "HIL loopback nominal (link, rates, mission)" \
            "$nav_seq ages imu=${imu_age}us gnss=${gnss_age}us baro=${baro_age}us rc=${rc_age}us"
    else
        report FAIL "HIL loopback nominal (link, rates, mission)" \
            "fc=$HIL_FC_RC rig=$HIL_RIG_RC nav=[$nav_seq] stalls=${fc_stalls:-?} crc=${fc_crc:-?} gaps=${fc_gaps:-?} ages imu=${imu_age:-?} gnss=${gnss_age:-?} baro=${baro_age:-?} rc=${rc_age:-?}"
    fi

    # -- 6b DShot capture analysed by an INDEPENDENT decoder -----------------
    if ! command -v "$PY" > /dev/null 2>&1; then
        report SKIP "HIL DShot capture analysis" "no python interpreter on PATH"
    else
        if (cd "$HIL_DIR" && "$PY" dshot_analyzer.py "$HIL_CAP" --expect-ticks 6000) \
             > /tmp/reg_hil_dshot.log 2>&1; then
            report PASS "HIL DShot capture analysis" \
                "$(grep -oE 'records=[0-9]+ ticks=[0-9]+ crc_ok=[0-9]+ crc_bad=[0-9]+' /tmp/reg_hil_dshot.log | tail -1)"
        else
            report FAIL "HIL DShot capture analysis" \
                "$(grep 'FAIL' /tmp/reg_hil_dshot.log | head -2)"
        fi
    fi

    # -- 6c fault injection over the link: latency and nav response ----------
    hil_run rc_loss 12000 --scenario rc_loss
    rig_applied="$(hil_metric "$HIL_RIG_LOG" 'fault_applied_us=[0-9]+' | grep -oE '[0-9]+$')"
    rig_latency="$(hil_metric "$HIL_RIG_LOG" 'fault_latency_us=[0-9]+' | grep -oE '[0-9]+$')"
    fc_rc_loss="$(grep 'sensor\[rc\]' "$HIL_FC_LOG" | grep -oE 'first_loss_us=[0-9]+' | cut -d= -f2)"
    nav_seq="$(grep 'nav:' "$HIL_FC_LOG" | sed 's/nav: \([A-Z]*\).*/\1/' | tr '\n' ' ')"
    if [ "$HIL_FC_RC" -eq 0 ] && [ "$HIL_RIG_RC" -eq 0 ] \
       && [ "${rig_latency:-1}" -le 1000 ] \
       && [ "${fc_rc_loss:-0}" -eq "${rig_applied:-1}" ] \
       && echo "$nav_seq" | grep -qE 'HOLD.*(LAND|RTL).*DONE'; then
        report PASS "HIL fault injection (rc_loss -> LAND)" \
            "applied=${rig_applied}us latency=${rig_latency}us fc_first_loss=${fc_rc_loss}us [$nav_seq]"
    else
        report FAIL "HIL fault injection (rc_loss -> LAND)" \
            "fc=$HIL_FC_RC rig=$HIL_RIG_RC applied=${rig_applied:-?} latency=${rig_latency:-?} fc_first_loss=${fc_rc_loss:-?} nav=[$nav_seq]"
    fi

    # -- 6d IMU fault injection over the link (SAF-001/003 abort path) --------
    hil_run imu_dropout 9000 --scenario imu_dropout
    imu_loss="$(grep 'sensor\[imu\]' "$HIL_FC_LOG" | grep -oE 'first_loss_us=[0-9]+' | cut -d= -f2)"
    nav_seq="$(grep 'nav:' "$HIL_FC_LOG" | sed 's/nav: \([A-Z]*\).*/\1/' | tr '\n' ' ')"
    if [ "$HIL_FC_RC" -eq 0 ] && [ "$HIL_RIG_RC" -eq 0 ] \
       && [ "${imu_loss:-0}" -eq 6000000 ] \
       && echo "$nav_seq" | grep -qE 'TAKEOFF.*HOLD.*ABORT'; then
        report PASS "HIL fault injection (imu_dropout -> ABORT)" \
            "imu_first_loss=${imu_loss}us [$nav_seq]"
    else
        report FAIL "HIL fault injection (imu_dropout -> ABORT)" \
            "fc=$HIL_FC_RC rig=$HIL_RIG_RC imu_first_loss=${imu_loss:-?} nav=[$nav_seq]"
    fi

    # -- 6e link robustness: injected wire corruption is rejected on CRC ----
    hil_run corrupt 9000 --scenario imu_dropout --corrupt-every 500
    inj="$(hil_metric "$HIL_RIG_LOG" 'corrupt_injected=[0-9]+' | cut -d= -f2)"
    fc_crc="$(hil_metric "$HIL_FC_LOG" 'crc_errors=[0-9]+' | cut -d= -f2)"
    nav_seq="$(grep 'nav:' "$HIL_FC_LOG" | sed 's/nav: \([A-Z]*\).*/\1/' | tr '\n' ' ')"
    # the FC exits 1 here on purpose: a rejected frame IS a link stall
    if [ "$HIL_RIG_RC" -eq 0 ] && [ "${inj:-0}" -gt 0 ] \
       && [ "${fc_crc:-0}" -eq "${inj:-1}" ] \
       && echo "$nav_seq" | grep -qE 'TAKEOFF.*ABORT'; then
        report PASS "HIL link robustness (CRC rejection + resync)" \
            "injected=${inj} rejected=${fc_crc} [$nav_seq]"
    else
        report FAIL "HIL link robustness (CRC rejection + resync)" \
            "rig=$HIL_RIG_RC injected=${inj:-?} rejected=${fc_crc:-?} nav=[$nav_seq]"
    fi

    # -- 6f watchdog interlock negative test (SAF-004): starving the feed MUST
    #        be detected. A pass here is the interlock proving it is armed.
    hil_run wdg 3000 -- --wdg-starve
    expiries="$(hil_metric "$HIL_FC_LOG" 'wdt_expiries=[0-9]+' | cut -d= -f2)"
    if [ "$HIL_FC_RC" -eq 3 ] && [ "${expiries:-0}" -ge 1 ]; then
        report PASS "HIL watchdog interlock (SAF-004 negative test)" \
            "fc exit $HIL_FC_RC (expected 3), wdt_expiries=${expiries}"
    else
        report FAIL "HIL watchdog interlock (SAF-004 negative test)" \
            "fc exit $HIL_FC_RC (expected 3), wdt_expiries=${expiries:-?}"
    fi
fi

# ---- 7. requirement coverage + architecture audit (Phase 23) ----------------
# TEST-004 (every baseline requirement has a status row and a traceability row),
# FW-001 (no algorithm file includes a HAL backend header) and FW-004 (no dynamic
# allocation in the control path). A new requirement without evidence now fails
# the regression instead of being forgotten.
if [ ! -f "$ROOT/08_testing/audit_requirements.py" ]; then
    report FAIL "requirement/architecture audit" "audit_requirements.py missing"
elif ! command -v "$PY" > /dev/null 2>&1; then
    report SKIP "requirement/architecture audit" "no python interpreter on PATH"
else
    if "$PY" "$ROOT/08_testing/audit_requirements.py" > /tmp/reg_audit.log 2>&1; then
        report PASS "requirement/architecture audit" \
            "$(grep -oE 'baseline requirements = [0-9]+' /tmp/reg_audit.log | head -1)"
    else
        report FAIL "requirement/architecture audit" \
            "$(grep 'FAIL' /tmp/reg_audit.log | head -2)"
    fi
fi

# ---- 8. manufacturing release package (MFG-001) -----------------------------
# Reproducible means: regenerate the package artifacts and then audit them
# against what the manifest pinned. The audit's own negative tests run too, so a
# check that silently stopped detecting anything fails here instead of looking
# green forever.
MFG_DIR="$ROOT/12_manufacturing"
if command -v gcc > /dev/null 2>&1; then
    if gcc -std=c11 -I"$FW/common" -I"$FW/common/utilities" -I"$FW/hal" \
              -I"$FW/flight_controller/configuration" \
              -o /tmp/reg_dump_params "$MFG_DIR/tools/dump_param_defaults.c" \
              "$FW/flight_controller/configuration/parameters.c" \
              "$FW/common/utilities/crc16.c" > /tmp/reg_param_gen.log 2>&1 \
       && /tmp/reg_dump_params "$MFG_DIR/parameters" >> /tmp/reg_param_gen.log 2>&1; then
        report PASS "parameter default file regeneration" \
            "rebuild from firmware defaults reproduced the pinned blob"
    else
        report FAIL "parameter default file regeneration" "$(tail -3 /tmp/reg_param_gen.log)"
    fi
else
    report SKIP "parameter default file regeneration" \
        "no host gcc - the audit still verifies the pinned blob byte-for-byte"
fi

if [ ! -f "$MFG_DIR/tools/audit_release_manifest.py" ]; then
    report FAIL "manufacturing release manifest audit" "audit_release_manifest.py missing"
elif ! command -v "$PY" > /dev/null 2>&1; then
    report SKIP "manufacturing release manifest audit" "no python interpreter on PATH"
else
    if "$PY" "$MFG_DIR/tools/audit_release_manifest.py" > /tmp/reg_mfg_audit.log 2>&1; then
        report PASS "manufacturing release manifest audit" \
            "$(grep -oE '[0-9]+ checks passed' /tmp/reg_mfg_audit.log | head -1)"
    else
        report FAIL "manufacturing release manifest audit" \
            "$(grep 'FAIL' /tmp/reg_mfg_audit.log | head -2)"
    fi
    if "$PY" "$MFG_DIR/tools/selftest_audit.py" > /tmp/reg_mfg_selftest.log 2>&1; then
        report PASS "release manifest audit negative tests" \
            "$(grep -oE '[0-9]+/[0-9]+ cases behaved as specified' /tmp/reg_mfg_selftest.log | tail -1)"
    else
        report FAIL "release manifest audit negative tests" \
            "$(grep '\[FAIL\]' /tmp/reg_mfg_selftest.log | head -2)"
    fi
fi

# ---- 9. H-gated hardware steps (TEST-002: skip with a CLEAR message) ------
if command -v arm-none-eabi-gcc > /dev/null 2>&1; then
    report SKIP "stm32 backend build" "toolchain present - wiring TODO (Phase 06/09)"
else
    report SKIP "stm32 backend build" \
        "H-gated: arm-none-eabi-gcc not installed - requires toolchain + board"
fi
if [ -e /dev/ttyUSB0 ] || [ -e /dev/ttyACM0 ] || ls //./COM* > /dev/null 2>&1; then
    report SKIP "HIL hardware execution (HIL-1..6)" \
        "H-gated: port detected but no rig mapping implemented yet (HIL_DESIGN.md)"
else
    report SKIP "HIL hardware execution (HIL-1..6)" \
        "H-gated: no board/rig present - host rig ran instead (step 6); see 07_simulation/hardware_in_the_loop/HIL_DESIGN.md"
fi

# ---- 10. documentation audit (Phase 26) ---------------------------------------
# Documents declare what they are (scaffold / task stub / content), the index is
# checked in both directions, and the counts quoted in prose have one home:
# 11_documentation/PROJECT_FACTS.json. This step re-checks the structure and
# then compares the counts below - measured by THIS run - against that file, so
# a stale number is a red build instead of a stale document.
DOCS_DIR="$ROOT/11_documentation"
if [ ! -f "$DOCS_DIR/tools/docs_audit.py" ]; then
    report FAIL "documentation audit" "docs_audit.py missing"
elif ! command -v "$PY" > /dev/null 2>&1; then
    report SKIP "documentation audit" "no python interpreter on PATH"
else
    unit_checks=""; unit_checks_total=""
    if [ -n "${SUMMARY:-}" ]; then
        unit_checks="$(echo "$SUMMARY" | cut -d/ -f1)"
        unit_checks_total="$(echo "$SUMMARY" | sed 's|.*/||; s| passed||')"
    fi
    executed=0; h_gated=0; reduced_env=0; scenarios=0; hil_host=0
    for r in "${RESULTS[@]}"; do
        case "$r" in
            SKIP:*) case "$r" in *"H-gated"*) h_gated=$((h_gated+1)) ;; *) reduced_env=1 ;; esac ;;
            *) executed=$((executed+1)) ;;
        esac
        case "$r" in *"scenario "*) scenarios=$((scenarios+1)) ;; esac
        case "$r" in
            *"HIL target build"*) ;;
            *"HIL "*) case "$r" in SKIP:*) ;; *) hil_host=$((hil_host+1)) ;; esac ;;
        esac
    done
    executed=$((executed+1))   # this step is an executed step too
    # plus the steps that report AFTER this one (V&V gate + its negative tests,
    # release manifest audit + its negative tests, final audit + its negative
    # tests): the facts file counts the whole run, not the run so far. Keep in
    # sync when a step is added after the documentation audit - a stale count
    # fails here.
    executed=$((executed+6))

    OBS=()
    obs() { [ -n "$2" ] && OBS+=("$1=$2"); }
    obs unit_checks "$unit_checks"
    obs unit_checks_total "$unit_checks_total"
    if [ "$reduced_env" -eq 0 ]; then
        # a host missing gcc/python runs fewer steps; the counts in the facts
        # file describe the documented environment, so do not fail on that here
        obs executed_steps "$executed"
        obs gated_skips "$h_gated"
    fi
    obs scenarios "$scenarios"
    obs hil_host_steps "$hil_host"
    obs gs_tests "$(grep -oE 'Ran [0-9]+ tests' /tmp/reg_gs_selftest.log 2>/dev/null | grep -oE '[0-9]+' | tail -1)"
    obs baseline_requirements "$(grep -oE 'baseline requirements = [0-9]+' /tmp/reg_audit.log 2>/dev/null | grep -oE '[0-9]+' | head -1)"
    obs algorithm_files "$(grep -oE 'algorithm files scanned = [0-9]+' /tmp/reg_audit.log 2>/dev/null | grep -oE '[0-9]+' | head -1)"
    obs package_audit_checks "$(grep -oE '[0-9]+ checks passed' /tmp/reg_mfg_audit.log 2>/dev/null | head -1 | cut -d' ' -f1)"
    obs package_audit_negative_cases "$(grep -oE '[0-9]+/[0-9]+ cases behaved' /tmp/reg_mfg_selftest.log 2>/dev/null | head -1 | cut -d/ -f1)"
    DOCS_ARGS=()
    if [ "${#OBS[@]}" -gt 0 ]; then
        DOCS_ARGS=(--observed "${OBS[@]}")
    fi
    if "$PY" "$DOCS_DIR/tools/docs_audit.py" "${DOCS_ARGS[@]}" > /tmp/reg_docs_audit.log 2>&1; then
        report PASS "documentation audit" \
            "$(grep -oE '[0-9]+ scaffold\(s\), [0-9]+ task stub\(s\)' /tmp/reg_docs_audit.log | head -1)"
    else
        doc_fail="$(grep 'FAIL' /tmp/reg_docs_audit.log | head -2 | tr '\n' '; ')"
        if [ -z "$doc_fail" ]; then
            doc_fail="$(tail -2 /tmp/reg_docs_audit.log | tr '\n' '; ')"
        fi
        report FAIL "documentation audit" "$doc_fail"
    fi
fi

# ---- 11. V&V review gate (Phase 27) -------------------------------------------
# Formal review: every baseline requirement is classified from its verification
# row (last status token wins, so a compound row's caveat counts), every evidence
# reference must resolve, and a critical requirement that is not verified must
# carry a written reason. The gate also re-checks that VNV_REVIEW.md still quotes
# its numbers. The selftest runs the same CLI against fixture trees, so a gate
# that silently stopped classifying fails here instead of looking green.
VNV="$ROOT/08_testing/vnv_gate.py"
if [ ! -f "$VNV" ]; then
    report FAIL "V&V requirement review gate" "vnv_gate.py missing"
elif ! command -v "$PY" > /dev/null 2>&1; then
    report SKIP "V&V requirement review gate" "no python interpreter on PATH"
else
    if "$PY" "$VNV" > /tmp/reg_vnv.log 2>&1; then
        report PASS "V&V requirement review gate" \
            "$(grep -oE 'unverified [0-9]+ \([^)]*\)' /tmp/reg_vnv.log | head -1)"
    else
        vnv_fail="$(grep '^FAIL' /tmp/reg_vnv.log | head -2 | tr '\n' '; ')"
        if [ -z "$vnv_fail" ]; then
            vnv_fail="$(tail -2 /tmp/reg_vnv.log | tr '\n' '; ')"
        fi
        report FAIL "V&V requirement review gate" "$vnv_fail"
    fi
    if "$PY" "$VNV" --selftest > /tmp/reg_vnv_selftest.log 2>&1; then
        report PASS "V&V gate negative tests" \
            "$(grep -oE '[0-9]+/[0-9]+ cases behaved as specified' /tmp/reg_vnv_selftest.log | tail -1)"
    else
        report FAIL "V&V gate negative tests" \
            "$(grep '^FAIL' /tmp/reg_vnv_selftest.log | head -2 | tr '\n' '; ')"
    fi
fi

# ---- 12. release manifest audit (Phase 28, OPS-001) --------------------------
# The release candidate pins the source tree digest and every distributed item.
# This step re-derives them from the tree, requires each gated artifact to be
# genuinely absent, and refuses to pass while OPS-001 still reads OPEN. Its
# negative tests run the same CLI against fixture releases.
REL="$ROOT/13_release"
if [ ! -f "$REL/tools/audit_release.py" ]; then
    report FAIL "release candidate manifest audit" "audit_release.py missing"
elif ! command -v "$PY" > /dev/null 2>&1; then
    report SKIP "release candidate manifest audit" "no python interpreter on PATH"
else
    if "$PY" "$REL/tools/audit_release.py" > /tmp/reg_rel.log 2>&1; then
        report PASS "release candidate manifest audit" \
            "$(grep -oE '[0-9]+ present item\(s\) digest-matched, [0-9]+ gated item\(s\) verified absent' /tmp/reg_rel.log | head -1)"
    else
        rel_fail="$(grep '^FAIL' /tmp/reg_rel.log | head -2 | tr '\n' '; ')"
        if [ -z "$rel_fail" ]; then
            rel_fail="$(tail -2 /tmp/reg_rel.log | tr '\n' '; ')"
        fi
        report FAIL "release candidate manifest audit" "$rel_fail"
    fi
    if "$PY" "$REL/tools/audit_release.py" --selftest > /tmp/reg_rel_selftest.log 2>&1; then
        report PASS "release candidate manifest audit negative tests" \
            "$(grep -oE '[0-9]+/[0-9]+ cases behaved as specified' /tmp/reg_rel_selftest.log | tail -1)"
    else
        report FAIL "release candidate manifest audit negative tests" \
            "$(grep '^FAIL' /tmp/reg_rel_selftest.log | head -2 | tr '\n' '; ')"
    fi
fi

# ---- 13. final A-Z audit (Phase 29) --------------------------------------------
# Every unresolved-work marker in the repository must be dispositioned in
# 08_testing/FINAL_AUDIT.md, two-way: an unregistered marker fails, and a
# register entry whose count changed fails. The selftest runs the same CLI
# against fixture trees, so a register that stopped checking is caught.
FINAL="$ROOT/08_testing"
if [ ! -f "$FINAL/final_audit.py" ]; then
    report FAIL "final audit (marker register)" "final_audit.py missing"
elif ! command -v "$PY" > /dev/null 2>&1; then
    report SKIP "final audit (marker register)" "no python interpreter on PATH"
else
    if "$PY" "$FINAL/final_audit.py" > /tmp/reg_final.log 2>&1; then
        report PASS "final audit (marker register)" \
            "$(grep -oE '[0-9]+ marker occurrence\(s\) in [0-9]+ file\(s\)' /tmp/reg_final.log | head -1)"
    else
        fin_fail="$(grep '^FAIL' /tmp/reg_final.log | head -2 | tr '\n' '; ')"
        if [ -z "$fin_fail" ]; then
            fin_fail="$(tail -2 /tmp/reg_final.log | tr '\n' '; ')"
        fi
        report FAIL "final audit (marker register)" "$fin_fail"
    fi
    if "$PY" "$FINAL/final_audit.py" --selftest > /tmp/reg_final_selftest.log 2>&1; then
        report PASS "final audit negative tests" \
            "$(grep -oE '[0-9]+/[0-9]+ cases behaved as specified' /tmp/reg_final_selftest.log | tail -1)"
    else
        report FAIL "final audit negative tests" \
            "$(grep '^FAIL' /tmp/reg_final_selftest.log | head -2 | tr '\n' '; ')"
    fi
fi

# ---- summary ----------------------------------------------------------------
echo "-----------------------------"
for r in "${RESULTS[@]}"; do echo "$r"; done
echo "-----------------------------"
if [ "$FAILURES" -gt 0 ]; then
    echo "REGRESSION: $FAILURES FAILED"
    exit 1
fi
echo "REGRESSION: ALL EXECUTED STEPS PASSED"
exit 0
