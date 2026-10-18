/*
 * Menus for the rotary knob (knob.h): lists of items, editors for values,
 * and full-screen pages.
 *
 * A menu is a tree of const menu_item_t tables, so it lives in flash and a
 * new entry is one line in a table (see dash_menu.c for the dashboard's):
 *
 *     static const menu_item_t k_display[] = {
 *         MENU_VALUE_ITEM("BRIGHTNESS", g_settings.brightness_pct, 10, 100, 10, "%", MENU_LIVE),
 *         MENU_TOGGLE_ITEM("SHIFT LIGHT FLASH", g_settings.shift_flash),
 *     };
 *
 * Knob controls:
 *
 *   Knob    List                 Editing a value         Page
 *   -----   ------------------   ---------------------   ----------------------------
 *   turn    move the highlight   change it               next/previous page in the list
 *   press   open / edit / run    keep the new value      back to the list
 *   hold    up a level           cancel: put it back     back to the list
 *
 * Press the knob on the home screen (gauges or pre-start guide) to open the
 * menu. Every list ends with a BACK row (EXIT at the top), so getting out
 * doesn't depend on knowing about holding the knob.
 *
 * A list or editor left alone for MENU_TIMEOUT_MS closes, so the menu never
 * hides the gauges for long. A value being edited then keeps what it was
 * set to if it is MENU_LIVE, and is dropped otherwise. Pages stay open:
 * they're there to be watched.
 */
#ifndef MENU_H
#define MENU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dash_data.h"
#include "knob.h"

#define MENU_MAX_DEPTH  4      /* nested lists, the top one included */
#define MENU_TIMEOUT_MS 10000u
#define MENU_TOAST_MS   2000u  /* how long an action's message shows */
#define MENU_ROWS       6      /* rows on screen; longer lists scroll */

/* Where the menu draws: under the shift lights and rev bar, which stay. */
#define MENU_HEADER_Y 114 /* centre line of the title */
#define MENU_BODY_Y0  140 /* lists and pages */
#define MENU_BODY_Y1  440

typedef enum {
    MENU_SUBMENU, /* opens the list `items` */
    MENU_PAGE,    /* a screen of its own: `draw` fills the body */
    MENU_VALUE,   /* *value from min to max in steps of `step`, shown with `unit` */
    MENU_CHOICE,  /* *value picks one of `names` (0 .. max) */
    MENU_TOGGLE,  /* *value 0 (OFF) or 1 (ON), flipped by a press */
    MENU_ACTION,  /* calls `run` */
} menu_type_t;

/* menu_item_t.flags */
#define MENU_LIVE (1u << 0) /* VALUE, CHOICE: write the setting at every click, not only when kept */

typedef struct menu_item menu_item_t;

struct menu_item {
    const char *label;
    menu_type_t type;
    uint8_t flags;            /* MENU_* */

    const menu_item_t *items; /* SUBMENU */
    uint8_t count;

    void (*draw)(const dash_data_t *d, uint32_t now_ms); /* PAGE */

    int16_t *value;           /* VALUE, CHOICE, TOGGLE: the setting it edits */
    int16_t min;
    int16_t max;
    int16_t step;             /* VALUE */
    const char *unit;         /* VALUE, or NULL */
    const char *const *names; /* CHOICE: max + 1 of them */

    const char *(*run)(void); /* ACTION: returns a message to confirm it, or NULL */
};

#define MENU_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define MENU_SUBMENU_ITEM(label_, items_) \
    {.label = (label_), .type = MENU_SUBMENU, .items = (items_), .count = (uint8_t)MENU_COUNT(items_)}

#define MENU_PAGE_ITEM(label_, draw_) {.label = (label_), .type = MENU_PAGE, .draw = (draw_)}

#define MENU_VALUE_ITEM(label_, var_, min_, max_, step_, unit_, flags_)                                   \
    {.label = (label_), .type = MENU_VALUE, .flags = (flags_), .value = &(var_), .min = (min_), .max = (max_), \
     .step = (step_), .unit = (unit_)}

#define MENU_CHOICE_ITEM(label_, var_, names_, flags_)                                                  \
    {.label = (label_), .type = MENU_CHOICE, .flags = (flags_), .value = &(var_), .min = 0,                \
     .max = (int16_t)(MENU_COUNT(names_) - 1u), .names = (names_)}

#define MENU_TOGGLE_ITEM(label_, var_) {.label = (label_), .type = MENU_TOGGLE, .value = &(var_), .min = 0, .max = 1}

#define MENU_ACTION_ITEM(label_, run_) {.label = (label_), .type = MENU_ACTION, .run = (run_)}

typedef enum {
    MENU_CLOSED,
    MENU_LIST,
    MENU_EDIT,
    MENU_SHOW_PAGE,
} menu_view_t;

/* Closed, with `root` (a MENU_SUBMENU) as the list a press opens. */
void menu_init(const menu_item_t *root);

/* Act on the knob, and close the menu once it has timed out. Call on every
 * knob poll, with or without anything in `ev`. */
void menu_update(const knob_event_t *ev, uint32_t now_ms);

void menu_close(void);

bool menu_is_open(void);
menu_view_t menu_view(void);

/* The highlighted item, the one being edited, or the open page. NULL on a
 * BACK/EXIT row or while closed. */
const menu_item_t *menu_current(void);

/* Draw the open menu into the current display list: the title, then the
 * list or the page in the body, and a hint for the knob along the bottom
 * (the bottom-right corner is left for the link status). */
void menu_draw(const dash_data_t *d, uint32_t now_ms);

#endif /* MENU_H */
