/*
 * The driver's settings, edited in the menu (dash_menu.c) and read by the
 * screens and the main loop.
 *
 * They're kept in RAM only, so every power-up starts from the defaults
 * below. Each one is an int16_t so a menu item can point straight at it.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

typedef enum {
    SPEED_KPH = 0,
    SPEED_MPH,
} speed_units_t;

#define SETTINGS_BRIGHTNESS_MIN 10 /* %: never dark enough to look off */
#define SETTINGS_ECU_MODE_MAX   9

typedef struct {
    int16_t brightness_pct; /* backlight, SETTINGS_BRIGHTNESS_MIN..100 */
    int16_t speed_units;    /* speed_units_t */
    int16_t shift_flash;    /* 1: the shift lights flash at the shift point; 0: they stay lit */
    int16_t ecu_mode;       /* 0..SETTINGS_ECU_MODE_MAX, sent to the ECU (ecu_link_set_mode()) */
} settings_t;

extern settings_t g_settings;

/* Put every setting back to its default. */
void settings_reset(void);

#endif /* SETTINGS_H */
