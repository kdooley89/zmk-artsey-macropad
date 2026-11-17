/*
 * Custom 128x32 OLED screen for the Paintbrush pad.
 *
 * While you type: output status (top-left), battery (top-right), layer name
 * (bottom-left), and a small pixel critter in the bottom-right corner that
 * blinks and hops on each key press.
 *
 * When ZMK reports idle (CONFIG_ZMK_IDLE_TIMEOUT), the status text hides and
 * the critter wanders the screen: walks, looks around, hops, blinks. After
 * MASCOT_SLEEP_AFTER_MS it walks to the middle and naps (Zz). After
 * MASCOT_OFF_AFTER_MS the display turns off to protect the OLED from burn-in.
 * Any key wakes everything up.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/atomic.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
#include <zmk/display/widgets/layer_status.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
#include <zmk/display/widgets/battery_status.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
#include <zmk/display/widgets/output_status.h>
#endif

/* ---- Tuning ------------------------------------------------------------ */

#define TICK_MS 100                             /* animation frame time */
#define MASCOT_SLEEP_AFTER_MS (2 * 60 * 1000)   /* idle time before the nap */
#define MASCOT_OFF_AFTER_MS (30 * 60 * 1000)    /* idle time before screen off */

/* ---- Geometry ---------------------------------------------------------- */

#define SCREEN_W 128
#define SCREEN_H 32
#define U 2                         /* one critter "pixel" = 2x2 screen pixels */
#define MASCOT_W (14 * U)
#define MASCOT_H (9 * U)
#define MASCOT_MAX_X (SCREEN_W - MASCOT_W)
#define MASCOT_Y (SCREEN_H - MASCOT_H)
#define MASCOT_HOME_X MASCOT_MAX_X
#define MASCOT_NAP_X ((SCREEN_W - MASCOT_W) / 2 - 8)

#define SLEEP_AFTER_TICKS (MASCOT_SLEEP_AFTER_MS / TICK_MS)
#define OFF_AFTER_TICKS (MASCOT_OFF_AFTER_MS / TICK_MS)

/* Match the mono theme: dark-on-light unless CONFIG_ZMK_DISPLAY_INVERT. */
#define FG (IS_ENABLED(CONFIG_ZMK_DISPLAY_INVERT) ? lv_color_white() : lv_color_black())
#define BG (IS_ENABLED(CONFIG_ZMK_DISPLAY_INVERT) ? lv_color_black() : lv_color_white())

/* ---- State ------------------------------------------------------------- */

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

/* Written by event listeners, read by the animation timer. */
static atomic_t want_idle;
static atomic_t key_pressed;

static lv_obj_t *mascot;
static lv_obj_t *eyes[2];
static lv_obj_t *legs[4];
static lv_obj_t *zz[2];
static lv_obj_t *status_objs[3];
static int status_count;

enum action { ACT_STAND, ACT_WALK, ACT_LOOK, ACT_HOP, ACT_NAP };

static bool idle;
static bool blanked;
static uint32_t idle_ticks;
static enum action act;
static int act_t, act_len;
static int pos_x, target_x, y_off, look, leg_phase, blink_left;
static uint32_t rng_state;

static const int8_t hop_curve[] = {-3, -5, -6, -5, -3, 0, 0, -3, -5, -6, -5, -3, 0};
static const int8_t key_hop_curve[] = {-2, -3, -2, 0};
static int key_hop_t = -1;

/* ---- Helpers ----------------------------------------------------------- */

static uint32_t rnd(void) {
    /* xorshift32; plenty for picking animations */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, color, LV_PART_MAIN);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}

static void set_hidden(lv_obj_t *o, bool hidden) {
    if (hidden) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

/*
 * The critter, in 2x2 "pixels" (14 x 9):
 *
 *     ..##########..
 *     ..##########..
 *     ..##o####o##..      o = eyes (holes in the body)
 *     ############## <- arms
 *     ##############
 *     ..##########..
 *     ..##########..
 *     ...#.#..#.#...      legs
 *     ...#.#..#.#...
 */
static void build_mascot(lv_obj_t *parent) {
    mascot = lv_obj_create(parent);
    lv_obj_remove_style_all(mascot);
    lv_obj_set_size(mascot, MASCOT_W, MASCOT_H);
    lv_obj_remove_flag(mascot, LV_OBJ_FLAG_SCROLLABLE);

    block(mascot, 2 * U, 0, 10 * U, 7 * U, FG);  /* body */
    block(mascot, 0, 3 * U, 2 * U, 2 * U, FG);   /* left arm */
    block(mascot, 12 * U, 3 * U, 2 * U, 2 * U, FG); /* right arm */

    eyes[0] = block(mascot, 4 * U, 2 * U, U, 2 * U, BG);
    eyes[1] = block(mascot, 9 * U, 2 * U, U, 2 * U, BG);

    static const int leg_x[] = {3, 5, 8, 10};
    for (int i = 0; i < 4; i++) {
        legs[i] = block(mascot, leg_x[i] * U, 7 * U, U, 2 * U, FG);
    }

    zz[0] = lv_label_create(parent);
    lv_label_set_text(zz[0], "z");
    zz[1] = lv_label_create(parent);
    lv_label_set_text(zz[1], "Z");
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_text_font(zz[i], lv_theme_get_font_small(parent), LV_PART_MAIN);
        set_hidden(zz[i], true);
    }
}

/* Push the current pose to the LVGL objects. */
static void render(void) {
    bool closed = (act == ACT_NAP && idle) || blink_left > 0;

    for (int i = 0; i < 2; i++) {
        int ex = (i ? 9 : 4) * U + look * U;
        if (closed) {
            lv_obj_set_pos(eyes[i], ex - 1, 3 * U);
            lv_obj_set_size(eyes[i], U + 2, 1);
        } else {
            lv_obj_set_pos(eyes[i], ex, 2 * U);
            lv_obj_set_size(eyes[i], U, 2 * U);
        }
    }

    /* Walking: alternate which pair of legs is lifted. */
    for (int i = 0; i < 4; i++) {
        bool lifted = leg_phase != 0 && ((i % 2) == (leg_phase - 1));
        lv_obj_set_height(legs[i], lifted ? U : 2 * U);
    }

    lv_obj_set_pos(mascot, pos_x, MASCOT_Y + y_off);
}

static void maybe_blink(void) {
    if (blink_left > 0) {
        blink_left--;
    } else if (rnd() % 40 == 0) {
        blink_left = 2;
    }
}

static void start_action(enum action a, int len) {
    act = a;
    act_t = 0;
    act_len = len;
    y_off = 0;
    leg_phase = 0;
}

static void pick_next_action(void) {
    look = 0;

    if (idle_ticks >= SLEEP_AFTER_TICKS) {
        if (pos_x != MASCOT_NAP_X) {
            target_x = MASCOT_NAP_X;
            start_action(ACT_WALK, 0);
        } else {
            start_action(ACT_NAP, 0);
        }
        return;
    }

    uint32_t r = rnd() % 10;
    if (r < 4) {
        do {
            target_x = rnd() % (MASCOT_MAX_X + 1);
        } while (abs(target_x - pos_x) < 16);
        start_action(ACT_WALK, 0);
    } else if (r < 6) {
        start_action(ACT_LOOK, 24);
    } else if (r < 8) {
        start_action(ACT_HOP, ARRAY_SIZE(hop_curve));
    } else {
        start_action(ACT_STAND, 15 + rnd() % 25);
    }
}

/* ---- Modes ------------------------------------------------------------- */

static void enter_idle(void) {
    idle = true;
    idle_ticks = 0;
    for (int i = 0; i < status_count; i++) {
        set_hidden(status_objs[i], true);
    }
    pick_next_action();
}

static void enter_active(void) {
    idle = false;
    if (blanked) {
        display_blanking_off(display);
        blanked = false;
    }
    for (int i = 0; i < status_count; i++) {
        set_hidden(status_objs[i], false);
    }
    set_hidden(zz[0], true);
    set_hidden(zz[1], true);
    pos_x = MASCOT_HOME_X;
    look = 0;
    blink_left = 0;
    start_action(ACT_STAND, 0);
}

static void tick_active(void) {
    if (atomic_cas(&key_pressed, 1, 0)) {
        key_hop_t = 0;
    }
    if (key_hop_t >= 0) {
        y_off = key_hop_curve[key_hop_t++];
        if (key_hop_t >= (int)ARRAY_SIZE(key_hop_curve)) {
            key_hop_t = -1;
        }
    }
    maybe_blink();
}

static void tick_idle(void) {
    idle_ticks++;

    if (idle_ticks == OFF_AFTER_TICKS) {
        display_blanking_on(display);
        blanked = true;
    }
    if (blanked) {
        return;
    }

    switch (act) {
    case ACT_STAND:
        maybe_blink();
        break;

    case ACT_WALK: {
        int dist = target_x - pos_x;
        int step = MIN(abs(dist), 2);
        look = dist > 0 ? 1 : -1;
        pos_x += dist > 0 ? step : -step;
        leg_phase = (act_t % 2) + 1;
        y_off = (act_t % 2) ? -1 : 0; /* little waddle */
        maybe_blink();
        if (pos_x == target_x) {
            act_len = act_t + 1; /* done after this frame */
        } else {
            act_len = act_t + 2;
        }
        break;
    }

    case ACT_LOOK:
        look = act_t < 8 ? -1 : act_t < 12 ? 0 : act_t < 20 ? 1 : 0;
        maybe_blink();
        break;

    case ACT_HOP:
        y_off = hop_curve[act_t];
        break;

    case ACT_NAP: {
        /* Breathe and float a couple of Zs. Never ends on its own. */
        int phase = act_t % 40;
        y_off = (act_t / 12) % 2;
        set_hidden(zz[0], false);
        lv_obj_set_pos(zz[0], pos_x + MASCOT_W + 2, 12 - phase / 6);
        set_hidden(zz[1], phase < 20);
        lv_obj_set_pos(zz[1], pos_x + MASCOT_W + 9, 4 - (phase - 20) / 8);
        act_len = act_t + 2;
        break;
    }
    }

    act_t++;
    if (act_t >= act_len) {
        pick_next_action();
    }
}

static void tick_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);

    bool should_idle = atomic_get(&want_idle);
    if (should_idle && !idle) {
        enter_idle();
    } else if (!should_idle && idle) {
        enter_active();
    }

    if (idle) {
        tick_idle();
    } else {
        tick_active();
    }

    if (!blanked) {
        render();
    }
}

/* ---- ZMK hooks --------------------------------------------------------- */

static int mascot_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *act_ev = as_zmk_activity_state_changed(eh);
    if (act_ev != NULL) {
        switch (act_ev->state) {
        case ZMK_ACTIVITY_ACTIVE:
            atomic_set(&want_idle, 0);
            break;
        case ZMK_ACTIVITY_IDLE:
            atomic_set(&want_idle, 1);
            break;
        case ZMK_ACTIVITY_SLEEP:
            /* Deep sleep is next; make sure the OLED isn't left lit. */
            display_blanking_on(display);
            break;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_position_state_changed *pos_ev = as_zmk_position_state_changed(eh);
    if (pos_ev != NULL && pos_ev->state) {
        atomic_set(&key_pressed, 1);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(paintbrush_mascot, mascot_listener);
ZMK_SUBSCRIPTION(paintbrush_mascot, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(paintbrush_mascot, zmk_position_state_changed);

#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
static struct zmk_widget_layer_status layer_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
static struct zmk_widget_battery_status battery_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
static struct zmk_widget_output_status output_status_widget;
#endif

static void add_status(lv_obj_t *screen, lv_obj_t *o, lv_align_t align) {
    lv_obj_set_style_text_font(o, lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_align(o, align, 0, 0);
    status_objs[status_count++] = o;
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
    zmk_widget_output_status_init(&output_status_widget, screen);
    add_status(screen, zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_LEFT);
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
    zmk_widget_battery_status_init(&battery_status_widget, screen);
    add_status(screen, zmk_widget_battery_status_obj(&battery_status_widget), LV_ALIGN_TOP_RIGHT);
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
    zmk_widget_layer_status_init(&layer_status_widget, screen);
    add_status(screen, zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_BOTTOM_LEFT);
#endif

    build_mascot(screen);

    rng_state = k_cycle_get_32() | 1;
    enter_active();
    render();
    lv_timer_create(tick_cb, TICK_MS, NULL);

    return screen;
}
