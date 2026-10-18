/*
 * The dashboard's CAN link to a rusEFI ECU (uaEFI or any rusEFI board).
 *
 *   ECU broadcast --CAN--> FDCAN interrupt --> receive queue
 *     --ecu_link_poll()--> rusefi_data_t (every broadcast field)
 *     --ecu_link_get_dash()--> dash_data_t --> dashboard_draw()
 *
 *   ecu_link_set_*() --> dash status frame --ecu_link_poll()--> CAN --> ECU
 *
 * ECU side, in TunerStudio's CAN settings: "Enable rusEFI CAN broadcast"
 * on, "rusEFI CAN data base address" 512 (0x200), "address type" 11 bit,
 * "Bitrate" as below. These are uaEFI's defaults. To act on the dash status
 * frame, load tools/rusefi/dash_link.lua as the ECU's Lua script.
 */
#ifndef ECU_LINK_H
#define ECU_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "dash_data.h"
#include "rusefi_can.h"

/* ---------------------------------------------------------------------------
 * Configuration. The first three must match the ECU.
 * ------------------------------------------------------------------------ */

#ifndef ECU_CAN_BITRATE
#define ECU_CAN_BITRATE 500000u /* "Bitrate" */
#endif
#ifndef ECU_RUSEFI_BASE_ID
#define ECU_RUSEFI_BASE_ID RUSEFI_DEFAULT_BASE_ID /* "rusEFI CAN data base address" */
#endif
#ifndef ECU_RUSEFI_EXT_ID
#define ECU_RUSEFI_EXT_ID 0 /* 1 if "rusEFI CAN data address type" is 29 bit */
#endif

/* A value is shown as "--" once its message is older than this. rusEFI
 * sends every 50 ms, slowed to every 250 ms while TunerStudio is connected
 * over CAN. */
#ifndef ECU_DATA_TIMEOUT_MS
#define ECU_DATA_TIMEOUT_MS 1000u
#endif

/* Tach smoothing time constant. The ECU sends rpm 20 times a second and the
 * screen redraws 60 times, so unsmoothed the needle visibly steps. 0 = off. */
#ifndef ECU_RPM_SMOOTH_MS
#define ECU_RPM_SMOOTH_MS 40u
#endif

/* ---------------------------------------------------------------------------
 * Dash status frame, dashboard -> ECU
 *
 * Sent every DASH_TX_PERIOD_MS, and straight away when an input or the mode
 * changes. 8 bytes:
 *
 *   byte 0     counter, +1 per frame (lets the ECU spot a frozen dash)
 *   byte 1     DASH_STATUS_* flags
 *   bytes 2-3  inputs, little-endian: bit n = input n active
 *   byte 4     mode (whatever the dash and the ECU script agree on)
 *   bytes 5-7  0, reserved
 * ------------------------------------------------------------------------ */

#ifndef DASH_TX_ENABLE
#define DASH_TX_ENABLE 1 /* 0: never transmit (the dash still acknowledges frames) */
#endif
#ifndef DASH_TX_ID
#define DASH_TX_ID 0x6D0u
#endif
#ifndef DASH_TX_EXT_ID
#define DASH_TX_EXT_ID 0
#endif
#define DASH_TX_PERIOD_MS 100u
#define DASH_TX_LEN       8u

#define DASH_STATUS_DISPLAY_OK  (1u << 0) /* the display is running */
#define DASH_STATUS_ECU_DATA_OK (1u << 1) /* the dash is receiving the ECU's broadcast */

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------ */

/* Start the CAN controller. Returns false if it didn't start; the link then
 * reports DASH_LINK_CAN_FAULT. */
bool ecu_link_init(uint32_t now_ms);

/* Decode everything received, send the status frame when due, and recover
 * the bus if needed. Call as often as possible from the main loop. */
void ecu_link_poll(uint32_t now_ms);

/* The dashboard's view of the ECU data at `now_ms`, with stale values
 * marked invalid. Call once per drawn frame (it also smooths the rpm). */
void ecu_link_get_dash(dash_data_t *d, uint32_t now_ms);

dash_link_t ecu_link_state(uint32_t now_ms);

/* Everything the ECU broadcasts, for screens that need more than dash_data_t. */
const rusefi_data_t *ecu_link_data(void);

/* Values reported to the ECU in the dash status frame. */
void ecu_link_set_display_ok(bool ok);
void ecu_link_set_inputs(uint16_t inputs);
void ecu_link_set_mode(uint8_t mode);

#endif /* ECU_LINK_H */
