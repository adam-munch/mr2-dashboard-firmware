#include "dash_menu.h"

#include "pages.h"
#include "settings.h"

/*
 *   MENU
 *     ENGINE DATA        page: oil pressure, lambda, MAP, intake air, throttle, timing
 *     DISPLAY       >
 *       BRIGHTNESS       10..100 %, applied while turning
 *       SPEED UNITS      KM/H or MPH
 *       SHIFT FLASH      ON: the shift lights flash at the shift point; OFF: steady
 *     ECU MODE           0..9, sent to the ECU once kept (never the values on the way)
 *     DIAGNOSTICS        page: firmware, knob, CAN bus counters
 *     RESTORE DEFAULTS
 *
 * To add an entry, add a line to a table: a setting to settings.h first,
 * then something that reads it.
 */

static const char *const k_speed_units[] = {
    [SPEED_KPH] = "KM/H",
    [SPEED_MPH] = "MPH",
};

static const menu_item_t k_display[] = {
    MENU_VALUE_ITEM("BRIGHTNESS", g_settings.brightness_pct, SETTINGS_BRIGHTNESS_MIN, 100, 10, "%", MENU_LIVE),
    MENU_CHOICE_ITEM("SPEED UNITS", g_settings.speed_units, k_speed_units, 0u),
    MENU_TOGGLE_ITEM("SHIFT FLASH", g_settings.shift_flash),
};

static const char *restore_defaults(void)
{
    settings_reset();
    return "DEFAULTS RESTORED";
}

static const menu_item_t k_top[] = {
    MENU_PAGE_ITEM("ENGINE DATA", pages_draw_engine),
    MENU_SUBMENU_ITEM("DISPLAY", k_display),
    MENU_VALUE_ITEM("ECU MODE", g_settings.ecu_mode, 0, SETTINGS_ECU_MODE_MAX, 1, NULL, 0u),
    MENU_PAGE_ITEM("DIAGNOSTICS", pages_draw_diagnostics),
    MENU_ACTION_ITEM("RESTORE DEFAULTS", restore_defaults),
};

const menu_item_t dash_menu = MENU_SUBMENU_ITEM("MENU", k_top);
