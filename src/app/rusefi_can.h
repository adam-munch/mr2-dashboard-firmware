/*
 * Decoder for rusEFI's CAN broadcast (TunerStudio: "Enable rusEFI CAN
 * broadcast"; "verbose CAN" in the rusEFI source). uaEFI boards enable it
 * by default.
 *
 * The ECU sends RUSEFI_MSG_COUNT frames, at IDs base+0 .. base+11 (base is
 * 0x200 by default), every 50 ms by default. Each frame is a packed
 * little-endian struct. The layout follows can_verbose.cpp and
 * rusEFI_CAN_verbose.dbc in the rusEFI source.
 *
 * Fields keep rusEFI's integer scaling, so nothing is rounded. The suffix
 * gives the scale: `map_kpa_x30` is kPa * 30, i.e. 1/30 kPa per count.
 */
#ifndef RUSEFI_CAN_H
#define RUSEFI_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "can.h"

#define RUSEFI_DEFAULT_BASE_ID 0x200u

typedef enum {
    RUSEFI_MSG_STATUS = 0, /* base + 0 */
    RUSEFI_MSG_SPEEDS,     /* base + 1 */
    RUSEFI_MSG_PEDAL_TPS,  /* base + 2 */
    RUSEFI_MSG_SENSORS1,   /* base + 3 */
    RUSEFI_MSG_SENSORS2,   /* base + 4 */
    RUSEFI_MSG_FUELING,    /* base + 5 */
    RUSEFI_MSG_FUELING2,   /* base + 6 */
    RUSEFI_MSG_FUELING3,   /* base + 7 */
    RUSEFI_MSG_CAMS,       /* base + 8 */
    RUSEFI_MSG_EGTS,       /* base + 9 */
    RUSEFI_MSG_KNOCK,      /* base + 10 */
    RUSEFI_MSG_STATUS11,   /* base + 11 (newer firmware only) */
    RUSEFI_MSG_COUNT
} rusefi_msg_t;

/* rusefi_data_t.flags (base + 0, byte 4). */
#define RUSEFI_FLAG_FUEL_SPARK_CUT (1u << 0) /* "RevLimAct": fuel or spark cut for any reason
                                              * (rev limiter, launch, limp mode, flood clear...) */
#define RUSEFI_FLAG_MAIN_RELAY     (1u << 1)
#define RUSEFI_FLAG_FUEL_PUMP      (1u << 2)
#define RUSEFI_FLAG_CHECK_ENGINE   (1u << 3)
#define RUSEFI_FLAG_O2_HEATER      (1u << 4)
#define RUSEFI_FLAG_LAMBDA_PROTECT (1u << 5)
#define RUSEFI_FLAG_FAN            (1u << 6)
#define RUSEFI_FLAG_FAN2           (1u << 7)

/* rusEFI sends -1 when a pedal/throttle/wastegate sensor is missing or failed. */
#define RUSEFI_PCT_NO_SENSOR (-100)
/* ... and -1 kPa (wrapped to unsigned) when there is no oil pressure sensor. */
#define RUSEFI_OIL_PRESS_NO_SENSOR 0xFFE2u

typedef struct {
    /* base + 0: status */
    uint16_t warning_count;
    uint16_t last_error;
    uint8_t flags;            /* RUSEFI_FLAG_* */
    uint8_t gear;             /* detected gear; 0 = neutral, or gear detection not set up */
    uint16_t distance_km_x10; /* trip distance */

    /* base + 1: speeds */
    uint16_t rpm;
    int16_t timing_deg_x50;   /* ignition advance */
    uint8_t inj_duty_pct_x2;
    uint8_t coil_duty_pct_x2;
    uint8_t speed_kph;
    uint8_t ethanol_pct;

    /* base + 2: RUSEFI_PCT_NO_SENSOR if the sensor is missing or failed */
    int16_t pedal_pct_x100;
    int16_t tps1_pct_x100;
    int16_t tps2_pct_x100;
    int16_t wastegate_pct_x100;

    /* base + 3: sensors */
    uint16_t map_kpa_x30;
    int16_t coolant_c;
    int16_t intake_c;
    int16_t aux1_c;
    int16_t aux2_c;
    int16_t mcu_c;
    uint8_t fuel_level_pct_x2;

    /* base + 4: sensors */
    uint16_t oil_press_kpa_x30; /* RUSEFI_OIL_PRESS_NO_SENSOR if there is none */
    int16_t oil_temp_c;
    int16_t fuel_temp_c;
    uint16_t battery_mv;

    /* base + 5: fueling */
    uint16_t cyl_airmass_mg;
    uint16_t airflow_kgh_x100;
    uint16_t inj_pulse_ms_x300;
    uint16_t knock_count;

    /* base + 6: fueling */
    uint16_t fuel_used_g;
    uint16_t fuel_flow_gps_x200;
    int16_t fuel_trim_pct_x100[2]; /* short-term trim, bank 1 and 2 */

    /* base + 7: fueling */
    uint16_t lambda_x10000[2]; /* sensor 1 and 2; AFR = lambda * 14.7 for petrol */
    uint16_t fuel_press_low_kpa_x30;
    uint16_t fuel_press_high_bar_x10;

    /* base + 8: cam positions, degrees. Per bank: intake actual, intake
     * target, exhaust actual, exhaust target. */
    int8_t cam_deg[8];

    /* base + 9: exhaust gas temperatures (rusEFI sends EGT 1 and 2) */
    uint16_t egt_c[8];

    /* base + 10: knock level per cylinder, dB */
    int8_t knock_db[8];

    /* base + 11 */
    bool brake_pedal;

    /* Bookkeeping */
    uint16_t seen;                    /* bit n: base + n has been received */
    uint32_t rx_ms[RUSEFI_MSG_COUNT]; /* when each message last arrived */
    uint32_t last_rx_ms;              /* newest of rx_ms[] */
    uint32_t frames;                  /* frames decoded */
} rusefi_data_t;

void rusefi_init(rusefi_data_t *d);

/*
 * Decode one frame of the broadcast at `base_id` (11-bit IDs, or 29-bit if
 * `ext`). Returns false, leaving `d` untouched, if the frame isn't part of
 * it or is too short. The frame's time_ms is recorded as its arrival time.
 */
bool rusefi_decode(rusefi_data_t *d, uint32_t base_id, bool ext, const can_frame_t *f);

/* True if message `msg` arrived less than `timeout_ms` before `now_ms`. */
bool rusefi_fresh(const rusefi_data_t *d, rusefi_msg_t msg, uint32_t now_ms, uint32_t timeout_ms);

#endif /* RUSEFI_CAN_H */
