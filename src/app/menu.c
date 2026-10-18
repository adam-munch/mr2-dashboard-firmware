#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "ui.h"

/*
 *   +----------------------------------------------------------------+
 *   | shift lights and rev bar (dashboard.c)                         |
 *   | TITLE                                        [alert, if any]   |
 *   | [ ITEM                                              VALUE ]   #|
 *   | [ ITEM                                                  > ]   #|
 *   |   ...                                                   scroll |
 *   | [ BACK                                                    ]    |
 *   | TURN: SELECT   PRESS: OPEN   HOLD: BACK          link status   |
 *   +----------------------------------------------------------------+
 *
 * Monochrome like the other screens: the highlight is a white outline, and
 * a value being edited turns into a white pill between arrows.
 */

#define ROW_X0  16
#define ROW_X1  784
#define ROW_H   46
#define ROW_GAP 4
#define ROW_PAD 20
#define PILL_W  220
#define BAR_X0  790 /* scroll bar, for lists longer than MENU_ROWS */
#define BAR_X1  794
#define FOOT_Y  (SCREEN_H - 16)

typedef struct {
    const menu_item_t *list; /* the MENU_SUBMENU whose items are shown */
    uint8_t cursor;          /* highlighted row; list->count is the BACK row */
    uint8_t top;             /* first row on screen */
} level_t;

static struct {
    const menu_item_t *root;
    menu_view_t view;
    uint8_t depth;
    level_t level[MENU_MAX_DEPTH];
    int16_t edit_value;    /* MENU_EDIT: the value as turned to... */
    int16_t edit_original; /* ...and as it was, for a cancel */
    uint32_t last_input_ms;
    const char *toast;     /* an action's message, shown in the footer */
    uint32_t toast_ms;
} s_menu;

/* ---------------------------------------------------------------------------
 * Navigation
 * ------------------------------------------------------------------------ */

static level_t *level(void)
{
    return &s_menu.level[s_menu.depth];
}

/* The items plus the BACK row. */
static uint32_t row_count(const level_t *lv)
{
    return lv->list->count + 1u;
}

static const menu_item_t *item_at(const level_t *lv, uint32_t row)
{
    return row < lv->list->count ? &lv->list->items[row] : NULL;
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void scroll_to_cursor(level_t *lv)
{
    if (lv->cursor < lv->top) {
        lv->top = lv->cursor;
    } else if (lv->cursor >= lv->top + MENU_ROWS) {
        lv->top = (uint8_t)(lv->cursor - MENU_ROWS + 1u);
    }
}

/* The nearest page after (dir > 0) or before row `from`, or -1. */
static int32_t neighbour_page(const level_t *lv, int32_t from, int32_t dir)
{
    for (int32_t i = from + dir; i >= 0 && i < (int32_t)lv->list->count; i += dir) {
        if (lv->list->items[i].type == MENU_PAGE) {
            return i;
        }
    }
    return -1;
}

void menu_init(const menu_item_t *root)
{
    memset(&s_menu, 0, sizeof s_menu);
    s_menu.root = root;
    s_menu.view = MENU_CLOSED;
}

void menu_close(void)
{
    s_menu.view = MENU_CLOSED;
    s_menu.toast = NULL;
}

bool menu_is_open(void)
{
    return s_menu.view != MENU_CLOSED;
}

menu_view_t menu_view(void)
{
    return s_menu.view;
}

const menu_item_t *menu_current(void)
{
    if (s_menu.view == MENU_CLOSED) {
        return NULL;
    }
    const level_t *lv = level();
    return item_at(lv, lv->cursor);
}

static void turn(int32_t clicks)
{
    level_t *lv = level();
    const menu_item_t *it = item_at(lv, lv->cursor);

    switch (s_menu.view) {
    case MENU_LIST:
        lv->cursor = (uint8_t)clamp((int32_t)lv->cursor + clicks, 0, (int32_t)row_count(lv) - 1);
        scroll_to_cursor(lv);
        break;

    case MENU_EDIT: {
        const int32_t step = it->type == MENU_VALUE && it->step > 0 ? it->step : 1;
        s_menu.edit_value = (int16_t)clamp(s_menu.edit_value + clicks * step, it->min, it->max);
        if ((it->flags & MENU_LIVE) != 0u) {
            *it->value = s_menu.edit_value;
        }
        break;
    }

    case MENU_SHOW_PAGE: {
        const int32_t dir = clicks > 0 ? 1 : -1;
        for (int32_t n = clicks; n != 0; n -= dir) {
            const int32_t next = neighbour_page(lv, lv->cursor, dir);
            if (next < 0) {
                break;
            }
            lv->cursor = (uint8_t)next;
        }
        scroll_to_cursor(lv);
        break;
    }

    case MENU_CLOSED:
        break;
    }
}

/* Up a level: a hold, or a press on the BACK row. Cancels an edit. */
static void back(void)
{
    const menu_item_t *it = menu_current();

    switch (s_menu.view) {
    case MENU_EDIT:
        *it->value = s_menu.edit_original;
        s_menu.view = MENU_LIST;
        break;
    case MENU_SHOW_PAGE:
        s_menu.view = MENU_LIST;
        break;
    case MENU_LIST:
        if (s_menu.depth > 0u) {
            s_menu.depth--;
        } else {
            menu_close();
        }
        break;
    case MENU_CLOSED:
        break;
    }
}

static void press(uint32_t now_ms)
{
    const menu_item_t *it = menu_current();

    if (s_menu.view == MENU_EDIT) {
        *it->value = s_menu.edit_value;
        s_menu.view = MENU_LIST;
        return;
    }
    if (s_menu.view == MENU_SHOW_PAGE) {
        s_menu.view = MENU_LIST;
        return;
    }
    if (it == NULL) {
        back();
        return;
    }

    switch (it->type) {
    case MENU_SUBMENU:
        if (s_menu.depth + 1u < MENU_MAX_DEPTH) {
            s_menu.depth++;
            *level() = (level_t){it, 0u, 0u};
        }
        break;
    case MENU_PAGE:
        s_menu.view = MENU_SHOW_PAGE;
        break;
    case MENU_VALUE:
    case MENU_CHOICE:
        s_menu.edit_original = *it->value;
        s_menu.edit_value = (int16_t)clamp(*it->value, it->min, it->max);
        s_menu.view = MENU_EDIT;
        break;
    case MENU_TOGGLE:
        *it->value = *it->value != 0 ? 0 : 1;
        break;
    case MENU_ACTION: {
        const char *msg = it->run != NULL ? it->run() : NULL;
        if (msg != NULL) {
            s_menu.toast = msg;
            s_menu.toast_ms = now_ms;
        }
        break;
    }
    }
}

void menu_update(const knob_event_t *ev, uint32_t now_ms)
{
    if (s_menu.view == MENU_CLOSED) {
        if (ev->press && s_menu.root != NULL) {
            s_menu.depth = 0u;
            s_menu.level[0] = (level_t){s_menu.root, 0u, 0u};
            s_menu.view = MENU_LIST;
            s_menu.last_input_ms = now_ms;
        }
        return;
    }

    if (ev->turn != 0 || ev->press || ev->hold) {
        s_menu.last_input_ms = now_ms;
    } else if (s_menu.view != MENU_SHOW_PAGE && (uint32_t)(now_ms - s_menu.last_input_ms) >= MENU_TIMEOUT_MS) {
        menu_close(); /* a live value keeps what it's set to; any other edit is dropped */
        return;
    }

    if (ev->turn != 0) {
        turn(ev->turn);
    }
    if (ev->hold) {
        back();
    } else if (ev->press) {
        press(now_ms);
    }
}

/* ---------------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------------ */

static void format_value(char *buf, size_t size, const menu_item_t *it, int16_t v)
{
    switch (it->type) {
    case MENU_VALUE:
        if (it->unit != NULL) {
            snprintf(buf, size, "%d %s", v, it->unit);
        } else {
            snprintf(buf, size, "%d", v);
        }
        break;
    case MENU_CHOICE:
        snprintf(buf, size, "%s", it->names[clamp(v, it->min, it->max)]);
        break;
    case MENU_TOGGLE:
        snprintf(buf, size, "%s", v != 0 ? "ON" : "OFF");
        break;
    default:
        buf[0] = '\0';
        break;
    }
}

/* The value being edited: inverted, between arrows that fade at the ends of
 * the range. A number also gets a bar showing where it is in the range. */
static void draw_editor(const menu_item_t *it, int32_t y0, int32_t y1)
{
    const int32_t cy = (y0 + y1) / 2;
    const int32_t px1 = ROW_X1 - 8;
    const int32_t px0 = px1 - PILL_W;
    char text[24];

    ui_rect(px0, y0 + 7, px1, y1 - 7, COL_TEXT, (y1 - y0 - 14) / 2);
    format_value(text, sizeof text, it, s_menu.edit_value);
    eve_color(COL_BG);
    eve_cmd_text((int16_t)((px0 + px1) / 2), (int16_t)cy, FONT_MEDIUM, OPT_CENTER, text);
    eve_color(s_menu.edit_value > it->min ? COL_BG : COL_TEXT_DIM);
    eve_cmd_text((int16_t)(px0 + 18), (int16_t)cy, FONT_MEDIUM, OPT_CENTER, "<");
    eve_color(s_menu.edit_value < it->max ? COL_BG : COL_TEXT_DIM);
    eve_cmd_text((int16_t)(px1 - 18), (int16_t)cy, FONT_MEDIUM, OPT_CENTER, ">");

    if (it->type == MENU_VALUE && it->max > it->min) {
        const int32_t bx0 = ROW_X0 + ROW_PAD;
        const int32_t bx1 = px0 - ROW_PAD;
        const int32_t fill = (s_menu.edit_value - it->min) * (bx1 - bx0) / (it->max - it->min);
        ui_rect(bx0, y1 - 9, bx1, y1 - 5, COL_TRACK, 2);
        ui_rect(bx0, y1 - 9, bx0 + fill, y1 - 5, COL_TEXT, 2);
    }
}

static void draw_row(const level_t *lv, uint32_t row, int32_t y0)
{
    const int32_t y1 = y0 + ROW_H;
    const int16_t cy = (int16_t)((y0 + y1) / 2);
    const bool selected = row == lv->cursor;
    const menu_item_t *it = item_at(lv, row);

    if (selected) {
        ui_rect(ROW_X0, y0, ROW_X1, y1, COL_TEXT, 8);
        ui_rect(ROW_X0 + 2, y0 + 2, ROW_X1 - 2, y1 - 2, COL_PANEL, 6);
    } else {
        ui_rect(ROW_X0, y0, ROW_X1, y1, COL_PANEL, 8);
    }

    if (it == NULL) {
        eve_color(selected ? COL_TEXT : COL_TEXT_FAINT);
        eve_cmd_text(ROW_X0 + ROW_PAD, cy, FONT_MEDIUM, OPT_CENTERY, s_menu.depth > 0u ? "BACK" : "EXIT");
        return;
    }
    eve_color(selected ? COL_TEXT : COL_TEXT_DIM);
    eve_cmd_text(ROW_X0 + ROW_PAD, cy, FONT_MEDIUM, OPT_CENTERY, it->label);

    if (it->type == MENU_SUBMENU || it->type == MENU_PAGE) {
        eve_cmd_text(ROW_X1 - ROW_PAD, cy, FONT_MEDIUM, OPT_RIGHTX | OPT_CENTERY, ">");
    } else if (selected && s_menu.view == MENU_EDIT) {
        draw_editor(it, y0, y1);
    } else if (it->value != NULL) {
        char text[24];
        format_value(text, sizeof text, it, *it->value);
        eve_cmd_text(ROW_X1 - ROW_PAD, cy, FONT_MEDIUM, OPT_RIGHTX | OPT_CENTERY, text);
    }
}

static void draw_list(const level_t *lv)
{
    const uint32_t rows = row_count(lv);
    for (uint32_t r = 0u; r < MENU_ROWS && lv->top + r < rows; r++) {
        draw_row(lv, lv->top + r, MENU_BODY_Y0 + (int32_t)r * (ROW_H + ROW_GAP));
    }

    if (rows > MENU_ROWS) {
        const int32_t y0 = MENU_BODY_Y0;
        const int32_t h = MENU_ROWS * (ROW_H + ROW_GAP) - ROW_GAP;
        ui_rect(BAR_X0, y0, BAR_X1, y0 + h, COL_TRACK, 2);
        ui_rect(BAR_X0, y0 + h * lv->top / (int32_t)rows, BAR_X1, y0 + h * (lv->top + MENU_ROWS) / (int32_t)rows,
                COL_TEXT_DIM, 2);
    }
}

/* What the knob does here, or an action's confirmation. */
static void draw_footer(const level_t *lv, uint32_t now_ms)
{
    if (s_menu.toast != NULL && (uint32_t)(now_ms - s_menu.toast_ms) < MENU_TOAST_MS) {
        ui_dot(ROW_X0 + 11, FOOT_Y, 11, COL_GREEN);
        ui_check(ROW_X0 + 11, FOOT_Y, COL_BG);
        eve_color(COL_TEXT);
        eve_cmd_text(ROW_X0 + 32, FOOT_Y, FONT_SMALL, OPT_CENTERY, s_menu.toast);
        return;
    }

    char hint[64];
    const char *up = s_menu.depth > 0u ? "BACK" : "EXIT";
    const menu_item_t *it = item_at(lv, lv->cursor);
    switch (s_menu.view) {
    case MENU_EDIT:
        snprintf(hint, sizeof hint, "TURN: ADJUST     PRESS: KEEP     HOLD: CANCEL");
        break;
    case MENU_SHOW_PAGE:
        if (neighbour_page(lv, lv->cursor, 1) >= 0 || neighbour_page(lv, lv->cursor, -1) >= 0) {
            snprintf(hint, sizeof hint, "TURN: NEXT PAGE     PRESS: BACK");
        } else {
            snprintf(hint, sizeof hint, "PRESS: BACK");
        }
        break;
    default: {
        const char *what = up;
        if (it != NULL) {
            static const char *const k_press[] = {
                [MENU_SUBMENU] = "OPEN", [MENU_PAGE] = "OPEN", [MENU_VALUE] = "EDIT",
                [MENU_CHOICE] = "EDIT",  [MENU_TOGGLE] = "SWITCH", [MENU_ACTION] = "RUN",
            };
            what = k_press[it->type];
        }
        snprintf(hint, sizeof hint, "TURN: SELECT     PRESS: %s     HOLD: %s", what, up);
        break;
    }
    }
    eve_color(COL_TEXT_FAINT);
    eve_cmd_text(ROW_X0, FOOT_Y, FONT_TINY, OPT_CENTERY, hint);
}

void menu_draw(const dash_data_t *d, uint32_t now_ms)
{
    if (s_menu.view == MENU_CLOSED) {
        return;
    }
    const level_t *lv = level();
    const menu_item_t *page = s_menu.view == MENU_SHOW_PAGE ? item_at(lv, lv->cursor) : NULL;

    eve_color(COL_TEXT);
    eve_cmd_text(ROW_X0, MENU_HEADER_Y, FONT_LARGE, OPT_CENTERY, page != NULL ? page->label : lv->list->label);

    if (page != NULL) {
        if (page->draw != NULL) {
            page->draw(d, now_ms);
        }
    } else {
        draw_list(lv);
    }
    draw_footer(lv, now_ms);
}
