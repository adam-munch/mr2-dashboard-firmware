#include "rusefi_can.h"

#include <string.h>

#define TEMP_OFFSET_C 40 /* rusEFI's PACK_ADD_TEMPERATURE */

/* Bytes each message must carry. rusEFI sends sizeof(struct) as the DLC. */
static const uint8_t k_min_dlc[RUSEFI_MSG_COUNT] = {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 1};

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int16_t s16(const uint8_t *p)
{
    return (int16_t)u16(p);
}

static int16_t temp_c(uint8_t raw)
{
    return (int16_t)(raw - TEMP_OFFSET_C);
}

void rusefi_init(rusefi_data_t *d)
{
    memset(d, 0, sizeof *d);
}

bool rusefi_decode(rusefi_data_t *d, uint32_t base_id, bool ext, const can_frame_t *f)
{
    if (f->ext != ext || f->id < base_id || f->id - base_id >= (uint32_t)RUSEFI_MSG_COUNT) {
        return false;
    }
    const rusefi_msg_t msg = (rusefi_msg_t)(f->id - base_id);
    if (f->dlc < k_min_dlc[msg]) {
        return false;
    }
    const uint8_t *p = f->data;

    switch (msg) {
    case RUSEFI_MSG_STATUS:
        d->warning_count = u16(&p[0]);
        d->last_error = u16(&p[2]);
        d->flags = p[4];
        d->gear = p[5];
        d->distance_km_x10 = u16(&p[6]);
        break;

    case RUSEFI_MSG_SPEEDS:
        d->rpm = u16(&p[0]);
        d->timing_deg_x50 = s16(&p[2]);
        d->inj_duty_pct_x2 = p[4];
        d->coil_duty_pct_x2 = p[5];
        d->speed_kph = p[6];
        d->ethanol_pct = p[7];
        break;

    case RUSEFI_MSG_PEDAL_TPS:
        d->pedal_pct_x100 = s16(&p[0]);
        d->tps1_pct_x100 = s16(&p[2]);
        d->tps2_pct_x100 = s16(&p[4]);
        d->wastegate_pct_x100 = s16(&p[6]);
        break;

    case RUSEFI_MSG_SENSORS1:
        d->map_kpa_x30 = u16(&p[0]);
        d->coolant_c = temp_c(p[2]);
        d->intake_c = temp_c(p[3]);
        d->aux1_c = temp_c(p[4]);
        d->aux2_c = temp_c(p[5]);
        d->mcu_c = temp_c(p[6]);
        d->fuel_level_pct_x2 = p[7];
        break;

    case RUSEFI_MSG_SENSORS2:
        /* bytes 0-1: padding */
        d->oil_press_kpa_x30 = u16(&p[2]);
        d->oil_temp_c = temp_c(p[4]);
        d->fuel_temp_c = temp_c(p[5]);
        d->battery_mv = u16(&p[6]);
        break;

    case RUSEFI_MSG_FUELING:
        d->cyl_airmass_mg = u16(&p[0]);
        d->airflow_kgh_x100 = u16(&p[2]);
        d->inj_pulse_ms_x300 = u16(&p[4]);
        d->knock_count = u16(&p[6]);
        break;

    case RUSEFI_MSG_FUELING2:
        d->fuel_used_g = u16(&p[0]);
        d->fuel_flow_gps_x200 = u16(&p[2]);
        d->fuel_trim_pct_x100[0] = s16(&p[4]);
        d->fuel_trim_pct_x100[1] = s16(&p[6]);
        break;

    case RUSEFI_MSG_FUELING3:
        d->lambda_x10000[0] = u16(&p[0]);
        d->lambda_x10000[1] = u16(&p[2]);
        d->fuel_press_low_kpa_x30 = u16(&p[4]);
        d->fuel_press_high_bar_x10 = u16(&p[6]);
        break;

    case RUSEFI_MSG_CAMS:
        for (unsigned i = 0u; i < 8u; i++) {
            d->cam_deg[i] = (int8_t)p[i];
        }
        break;

    case RUSEFI_MSG_EGTS:
        for (unsigned i = 0u; i < 8u; i++) {
            d->egt_c[i] = (uint16_t)(p[i] * 5u);
        }
        break;

    case RUSEFI_MSG_KNOCK:
        for (unsigned i = 0u; i < 8u; i++) {
            d->knock_db[i] = (int8_t)p[i];
        }
        break;

    case RUSEFI_MSG_STATUS11:
        d->brake_pedal = (p[0] & 1u) != 0u;
        break;

    case RUSEFI_MSG_COUNT:
        return false;
    }

    d->seen |= (uint16_t)(1u << msg);
    d->rx_ms[msg] = f->time_ms;
    if (d->frames == 0u || (int32_t)(f->time_ms - d->last_rx_ms) > 0) {
        d->last_rx_ms = f->time_ms;
    }
    d->frames++;
    return true;
}

bool rusefi_fresh(const rusefi_data_t *d, rusefi_msg_t msg, uint32_t now_ms, uint32_t timeout_ms)
{
    /* Signed: a frame can arrive (and be stamped) just after `now_ms` was read. */
    const int32_t age = (int32_t)(now_ms - d->rx_ms[msg]);
    return (d->seen & (1u << msg)) != 0u && age < (int32_t)timeout_ms;
}
