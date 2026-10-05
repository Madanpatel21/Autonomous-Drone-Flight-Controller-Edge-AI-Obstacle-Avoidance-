#ifndef HAL_HIL_H
#define HAL_HIL_H

/* HIL HAL backend (Phase 22, DEC-019): the real application running with every
 * sensor, actuator and storage access served over the framed HIL link to a host
 * rig that owns the vehicle world.
 *
 * Timing model (HIL_DESIGN.md section 2):
 *   - the RIG is the clock master. Every rig sensor frame carries the rig time
 *     and hal_time_us() serves THAT value, so the FC's time base is the host's
 *     virtual clock, not a free-running local counter.
 *   - one control tick == one request/response exchange, i.e. 1 kHz in rig time.
 *   - the control path never blocks on the socket: hal_*_read() serves cached
 *     data from the last frame and hal_actuator_write() only enqueues. Socket
 *     I/O happens in hil_link_poll(), which the HIL entry point calls at the
 *     tick boundary (equivalent to the STM32 DMA interrupts it replaces).
 *
 * NOT HARDWARE EVIDENCE. This target proves the link, the timing bookkeeping,
 * the fault-injection path and the safety interlocks on a host; it says nothing
 * about an STM32's real timing, buses or electrical behaviour (HIL-1..HIL-6 stay
 * H-gated). */

#include "hal_interfaces.h"
#include "hil_link.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t ticks;                 /* control ticks executed */
    uint32_t sensor_frames;         /* rig frames consumed */
    uint32_t frames_sent;           /* frames pushed to the rig */
    uint32_t actuator_writes;
    uint32_t tx_drops;              /* TX ring full (must stay 0) */
    uint32_t link_stalls;           /* ticks with no new rig frame */
    uint32_t wdt_expiries;          /* SAF-004 watchdog expiries */
    uint32_t wdg_timeout_ms;
    uint64_t first_sample_age_max_us[HIL_SENSOR_COUNT];
    uint64_t first_loss_us[HIL_SENSOR_COUNT];   /* 0 = never lost */
    uint64_t wall_elapsed_ms;
    uint32_t wire_bytes_rx, wire_bytes_tx;
    uint32_t crc_errors, seq_gaps, resyncs;    /* from the RX ring */
    uint32_t tx_send_calls, tx_send_err;       /* link flush diagnostics */
    int      tx_send_err_last;
    uint32_t rx_calls;
    hil_ring_t ring_stats;
} hil_stats_t;

/* Connect to the rig (blocking connect with timeout, host-paced). */
int  hil_link_init(const char *host, uint16_t port, uint32_t timeout_ms);
void hil_link_shutdown(void);

/* Non-blocking socket I/O: drain the socket into the RX ring, dispatch complete
 * frames into the caches, flush the TX ring, service the emulated watchdog. */
void hil_link_poll(void);

/* Wait (bounded) for the next rig sensor frame; false on timeout. */
bool hil_link_wait_sensor(uint32_t timeout_ms);

/* Block up to timeout_ms until the link has data (select-based, millisecond
 * accurate - a sleep would quantise to the OS timer granularity). */
void hil_link_wait_io(uint32_t timeout_ms);

/* Synchronous request/response (init-time only, never in the control path). */
bool hil_link_request(uint8_t type, const uint8_t *payload, uint16_t len,
                      hil_msg_t *resp);

/* Fault injection and scenario selection, forwarded to the rig. */
void hil_link_fault(const char *name, bool active);
void hil_link_schedule_fault(const char *name, uint64_t at_us);
void hil_link_scenario(const char *name);
void hil_link_request_world(void);   /* rig truth snapshot for correlation */

void hil_link_stats(hil_stats_t *out);
void hil_link_report(void);          /* one-line metric summary (machine-greppable) */

/* Tick accounting used by the HIL entry point (one per control tick). */
void hil_link_tick(bool got_new_frame);
/* Debug-only (HIL_DEBUG_TRACE) integrity check on the RX ring indices. */
void hil_link_ring_sanity(const char *where);
void hil_link_set_wall_elapsed_ms(uint64_t ms);

/* Shared time base (rig clock), and true vehicle truth from the last WORLD reply. */
uint64_t hil_link_time_us(void);
bool     hil_link_world(uint64_t *t_us, float world[11]);

#endif /* HAL_HIL_H */