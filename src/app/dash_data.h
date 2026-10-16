/* Values shown on the dashboard. */
#ifndef DASH_DATA_H
#define DASH_DATA_H

#include <stdint.h>

/* dash_data_t.valid: which values are current. The others (no data yet, or
 * the ECU stopped sending them) are drawn as "--". */
#define DASH_VALID_RPM     (1u << 0)
#define DASH_VALID_SPEED   (1u << 1)
#define DASH_VALID_COOLANT (1u << 2)
#define DASH_VALID_FUEL    (1u << 3)
#define DASH_VALID_BATTERY (1u << 4)
#define DASH_VALID_GEAR    (1u << 5)
#define DASH_VALID_ALL     0x3Fu

/* dash_data_t.warnings: warning lamps. */
#define DASH_WARN_CHECK_ENGINE (1u << 0) /* the ECU's check engine output is on */
#define DASH_WARN_LIMITER      (1u << 1) /* the ECU is cutting fuel or spark */

/* Where the values come from. Anything but OK is shown in the status line. */
typedef enum {
    DASH_LINK_OK = 0,    /* live data from the ECU */
    DASH_LINK_DEMO,      /* built-in simulation */
    DASH_LINK_WAITING,   /* nothing from the ECU since power-up */
    DASH_LINK_LOST,      /* the ECU stopped sending */
    DASH_LINK_CAN_FAULT, /* CAN controller not running, or off the bus */
} dash_link_t;

typedef struct {
    uint16_t rpm;
    uint16_t speed_kph;
    int16_t coolant_c;
    uint8_t fuel_pct;
    uint16_t battery_mv;
    uint8_t gear;     /* 0 = neutral, 1..9 */
    uint8_t valid;    /* DASH_VALID_* */
    uint8_t warnings; /* DASH_WARN_* */
    dash_link_t link;
} dash_data_t;

#endif /* DASH_DATA_H */
