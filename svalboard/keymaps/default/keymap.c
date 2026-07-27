/*
Copyright 2023 Morgan Venable @_claussen

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../keymap_support.c"
#include "keycodes.h"
#include "quantum_keycodes.h"
#include QMK_KEYBOARD_H
#include "svalboard.h"
#include "vial.h"
#include "dynamic_keymap.h"
#include "pointing_device.h"
#include <stdbool.h>
#include <stdint.h>

// Tap dance declarations
typedef enum {
    TD_NONE,
    TD_UNKNOWN,
    TD_SINGLE_TAP,
    TD_SINGLE_HOLD,
    TD_DOUBLE_TAP,
    TD_DOUBLE_HOLD,
    TD_DOUBLE_SINGLE_TAP, // Send two single taps
    TD_TRIPLE_TAP,
    TD_TRIPLE_HOLD
} td_state_t;

typedef struct {
    bool       is_press_action;
    td_state_t state;
} td_tap_t;

// Tap dance enums
enum {
    SHIFT_CAPS,
    GUI_LAYER,
};

enum layer {
    DVORAK,                      // 0
    LAYERSEL,                    // 1
    QWERTY,                      // 2
    SYMBOLS,                     // 3
    NAVNUM,                      // 4
    LOL,                         // 5
    FKEYS,                       // 6
    UTILS,                       // 7
    MBO = MH_AUTO_BUTTONS_LAYER, // 8
};

td_state_t cur_dance(tap_dance_state_t *state);

// For the x tap dance. Put it here so it can be used in any keymap
void hold_shift_finished(tap_dance_state_t *state, void *user_data);
void hold_shift_reset(tap_dance_state_t *state, void *user_data);

// GUI_LAYER: tap = delete ; tap and hold: momentary layer switch
void gui_layer_finished(tap_dance_state_t *state, void *user_data);
void gui_layer_reset(tap_dance_state_t *state, void *user_data);

#define LAYER_COLOR(name, color) rgblight_segment_t const(name)[] = RGBLIGHT_LAYER_SEGMENTS({0, 2, color})

LAYER_COLOR(layer0_colors, HSV_PURPLE);
LAYER_COLOR(layer1_colors, HSV_YELLOW);
LAYER_COLOR(layer2_colors, HSV_MAGENTA);
LAYER_COLOR(layer3_colors, HSV_ORANGE);
LAYER_COLOR(layer4_colors, HSV_TEAL);
LAYER_COLOR(layer5_colors, HSV_RED);
LAYER_COLOR(layer6_colors, HSV_RED);
LAYER_COLOR(layer7_colors, HSV_SPRINGGREEN);
// layers 8-14 are unreachable (enum layer jumps UTILS=7 -> MBO=15), but
// sval_rgb_layers[] below is positional by QMK layer number, so these still
// need to exist just to hold array slots 8-14; colors here are irrelevant.
LAYER_COLOR(layer8_colors, HSV_PINK);
LAYER_COLOR(layer9_colors, HSV_PURPLE);
LAYER_COLOR(layer10_colors, HSV_CORAL);
LAYER_COLOR(layer11_colors, HSV_SPRINGGREEN);
LAYER_COLOR(layer12_colors, HSV_TEAL);
LAYER_COLOR(layer13_colors, HSV_TURQUOISE);
LAYER_COLOR(layer14_colors, HSV_YELLOW);
LAYER_COLOR(layer15_colors, HSV_TURQUOISE); // MBO
#undef LAYER_COLOR

const rgblight_segment_t *const __attribute((weak)) sval_rgb_layers[] = RGBLIGHT_LAYERS_LIST(layer0_colors, layer1_colors, layer2_colors, layer3_colors, layer4_colors, layer5_colors, layer6_colors, layer7_colors, layer8_colors, layer9_colors, layer10_colors, layer11_colors, layer12_colors, layer13_colors, layer14_colors, layer15_colors);

layer_state_t default_layer_state_set_user(layer_state_t state) {
    rgblight_set_layer_state(0, layer_state_cmp(state, 0));
    return state;
}

// Svalboard's own auto-mouse gate (keymap_support.c: mouse_mode()), not the
// stock qmk automouse feature.
static bool auto_mouse_saved_state     = true;
static bool auto_mouse_override_active = false;

// Read by keymap_support.c's pointing_device_task_combined_user() to drop
// trackball movement entirely while NAVNUM is active.
bool pointer_movement_locked = false;

layer_state_t layer_state_set_user(layer_state_t state) {
    for (int i = 0; i < RGBLIGHT_LAYERS; ++i) {
        rgblight_set_layer_state(i, layer_state_cmp(state, i));
    }

    pointer_movement_locked = layer_state_cmp(state, NAVNUM);

    // Do not allow mouse if navnum layer is on
    if (pointer_movement_locked) {
        if (!auto_mouse_override_active) {
            auto_mouse_saved_state     = global_saved_values.auto_mouse;
            auto_mouse_override_active = true;
        }
        // mouse_mode()'s off branch is itself gated on auto_mouse, so clear the
        // mouse overlay bit here first or it can never be turned back off.
        state &= ~((layer_state_t)1 << MH_AUTO_BUTTONS_LAYER);
        global_saved_values.auto_mouse = false;
        mouse_mode_enabled             = false;
    } else if (auto_mouse_override_active) {
        global_saved_values.auto_mouse = auto_mouse_saved_state;
        auto_mouse_override_active     = false;
    }

    return state;
}

// vial.c (quantum/vial.c) is the actual definition of this global; nothing
// in any header declares it externally, so keymap.c needs its own extern.
extern tap_dance_action_t tap_dance_actions[];

void keyboard_post_init_user(void) {
    rgblight_layers = sval_rgb_layers;

    // As vial owns tap_dance_actions[], need to initialize it as so
    tap_dance_actions[SHIFT_CAPS] = (tap_dance_action_t)ACTION_TAP_DANCE_FN_ADVANCED(NULL, hold_shift_finished, hold_shift_reset);
    tap_dance_actions[GUI_LAYER]  = (tap_dance_action_t)ACTION_TAP_DANCE_FN_ADVANCED(NULL, gui_layer_finished, gui_layer_reset);

    // Declare GUI_LAYER's tapping term here instead of setting it by hand in vial
    vial_tap_dance_entry_t gui_layer_td;
    dynamic_keymap_get_tap_dance(GUI_LAYER, &gui_layer_td);
    gui_layer_td.custom_tapping_term = 200; // mod layer's term
    dynamic_keymap_set_tap_dance(GUI_LAYER, &gui_layer_td);
}

// Keep gui layer switcher locked until ESC is pressed
static bool gui_layer_locked = false;

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Escape cancels a locked GUI_LAYER instead of being sent through it:
    // swallow the Escape entirely and tear the layer/mod back down.
    if (keycode == KC_ESCAPE && record->event.pressed && gui_layer_locked) {
        layer_off(NAVNUM);
        unregister_code(KC_LGUI);
        gui_layer_locked = false;
        return false;
    }
    return true;
}

td_state_t cur_dance(tap_dance_state_t *state) {
    if (state->count == 1) {
        if (state->interrupted || !state->pressed)
            return TD_SINGLE_TAP;
        else // key has not been interrupted, but the key is still held
            return TD_SINGLE_HOLD;
    } else if (state->count == 2) {
        // TD_DOUBLE_SINGLE_TAP is to distinguish between typing "pepper", and
        // actually wanting a double tap action when hitting 'pp'. Suggested use
        // case for this return value is when you want to send two keystrokes of the
        // key, and not the 'double tap' action/macro.
        if (state->interrupted) return TD_DOUBLE_SINGLE_TAP;
        // Second press is still held down: this is a "tap-then-hold", not a clean
        // double tap.
        else if (state->pressed)
            return TD_DOUBLE_HOLD;
        else
            return TD_DOUBLE_TAP;
    }

    // Assumes no one is trying to type the same letter three times (at least not
    // quickly). If your tap dance key is 'KC_W', and you want to type "www."
    // quickly - then you will need to add an exception here to return a
    // 'TD_TRIPLE_SINGLE_TAP', and define that enum just like
    // 'TD_DOUBLE_SINGLE_TAP' if (state->count == 3) {
    //     if (state->interrupted || !state->pressed) return TD_TRIPLE_TAP;
    //     else return TD_TRIPLE_HOLD;
    // } else
    return TD_UNKNOWN;
}

static td_tap_t hold_shift_state = {.is_press_action = true, .state = TD_NONE};

void hold_shift_finished(tap_dance_state_t *state, void *user_data) {
    hold_shift_state.state = cur_dance(state); // grab current state

    switch (hold_shift_state.state) {
        case TD_SINGLE_HOLD:
            register_code(KC_LSFT);
            break;
        case TD_DOUBLE_TAP:
            caps_word_toggle();
            break;
        default:
            break;
    }
}

void hold_shift_reset(tap_dance_state_t *state, void *user_data) {
    switch (hold_shift_state.state) {
        case TD_SINGLE_HOLD:
            unregister_code(KC_LSFT);
            break;
        default:
            break;
    }
    hold_shift_state.state = TD_NONE; // reset tap dance state
}

static td_tap_t gui_layer_state = {.is_press_action = true, .state = TD_NONE};

void gui_layer_finished(tap_dance_state_t *state, void *user_data) {
    gui_layer_state.state = cur_dance(state);

    switch (gui_layer_state.state) {
        case TD_SINGLE_TAP:
            tap_code(KC_DELETE);
            break;
        case TD_DOUBLE_HOLD:
            // Nav to the layer
            layer_on(NAVNUM);
            register_code(KC_LGUI);
            gui_layer_locked = true;
            break;
        default:
            break;
    }
}

void gui_layer_reset(tap_dance_state_t *state, void *user_data) {
    switch (gui_layer_state.state) {
        default:
            break;
    }
    gui_layer_state.state = TD_NONE;
}

// clang-format off
const uint16_t PROGMEM keymaps[DYNAMIC_KEYMAP_LAYER_COUNT][MATRIX_ROWS][MATRIX_COLS] = {
    [DVORAK] = LAYOUT(
        /*         Center            North           East              South           West              Double*/
        /*R1*/     KC_H            , KC_G            , LSFT(KC_SLASH)  , KC_M          , KC_D            , KC_NO           ,
        /*R2*/     KC_T            , KC_C            , KC_B            , KC_W          , KC_F            , KC_NO           ,
        /*R3*/     KC_N            , KC_R            , KC_NO           , KC_V          , KC_NO           , KC_NO           ,
        /*R4*/     KC_S            , KC_L            , TG(UTILS)       , KC_Z          , KC_NO           , KC_NO           ,

        /*L1*/     KC_U            , KC_P            , KC_I            , KC_K          , LSFT(KC_1)      , KC_NO           ,
        /*L2*/     KC_E            , KC_DOT          , KC_Y            , KC_J          , KC_X            , KC_NO           ,
        /*L3*/     KC_O            , KC_COMMA        , KC_NO           , KC_Q          , TO(MBO)         , KC_NO           ,
        /*L4*/     KC_A            , KC_SCLN         , TG(NAVNUM)      , KC_QUOTE      , TO(LAYERSEL)    , KC_NO           ,

        /*         Down                     Pad             Up                Nail            Knuckle           DoubleDown*/
        /*RT*/     TD(SHIFT_CAPS)         , KC_ENTER        , KC_LALT       , KC_BSPC       , LCTL(KC_B)      , TG(UTILS)       ,
        /*LT*/     LT(SYMBOLS, KC_SPACE)  , LGUI_T(KC_ESC)  , KC_TAB        , TD(GUI_LAYER) , KC_LCTL         , TO(DVORAK)
    ),

    [LAYERSEL] = LAYOUT(
        /*         Center            North           East              South           West              Double*/
        /*R1*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*R2*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*R3*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*R4*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,

        /*L1*/     TG(UTILS)       , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*L2*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*L3*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*L4*/     TG(QWERTY)      , KC_NO           , KC_NO           , TO(LOL)       , KC_NO           , KC_NO           ,

        /*         Down                     Pad             Up                Nail            Knuckle           DoubleDown*/
        /*RT*/     KC_TRNS         , KC_TRNS         , KC_TRNS         , KC_TRNS       , KC_TRNS         , KC_TRNS         ,
        /*LT*/     TO(DVORAK)      , KC_TRNS         , KC_TRNS         , KC_TRNS       , KC_TRNS         , KC_NO
    ),

    [QWERTY] = LAYOUT(
        /*         Center            North           East              South           West              Double*/
        /*R1*/     KC_J            , KC_U            , LSFT(KC_SLASH)  , KC_M          , KC_H            , KC_NO           ,
        /*R2*/     KC_K            , KC_I            , KC_N            , KC_COMMA      , KC_Y            , KC_NO           ,
        /*R3*/     KC_L            , KC_O            , KC_NO           , KC_DOT        , KC_Y            , KC_NO           ,
        /*R4*/     KC_SCLN         , KC_P            , TG(UTILS)       , KC_SLASH      , KC_NO           , KC_NO           ,

        /*L1*/     KC_F            , KC_R            , KC_G            , KC_V          , LSFT(KC_1)      , KC_NO           ,
        /*L2*/     KC_D            , KC_E            , KC_T            , KC_C          , KC_B            , KC_NO           ,
        /*L3*/     KC_S            , KC_W            , KC_NO           , KC_X          , TO(MBO)         , KC_NO           ,
        /*L4*/     KC_A            , KC_Z            , TG(NAVNUM)      , KC_Q          , TO(LAYERSEL)    , KC_NO           ,

        /*         Down                     Pad                 Up              Nail             Knuckle           DoubleDown*/
        /*RT*/     TD(SHIFT_CAPS)  ,        KC_ENTER            , KC_LALT       , KC_BSPC        , LCTL(KC_B)      , TG(UTILS)       ,
        /*LT*/     LT(SYMBOLS, KC_SPACE),   LGUI_T(KC_ESC)      , KC_TAB        , TD(GUI_LAYER)  , KC_LCTL         , TO(DVORAK)
    ),

    [SYMBOLS] = LAYOUT(
        /*         Center                North               East              South               West              Double*/
        /*R1*/     LSFT(KC_RBRC)       , KC_NO             , KC_EQUAL        , KC_NO             , LSFT(KC_DOT)    , KC_NO           ,
        /*R2*/     LSFT(KC_0)          , LSFT(KC_BSLS)     , KC_NO           , LSFT(KC_MINUS)    , KC_NO           , KC_NO           ,
        /*R3*/     KC_SLASH            , LSFT(KC_6)        , KC_NO           , LSFT(KC_3)        , KC_NO           , KC_NO           ,
        /*R4*/     KC_SCLN             , KC_NO             , LSFT(KC_4)      , KC_NO             , KC_NO           , KC_NO           ,

        /*L1*/     LSFT(KC_LBRC)       , KC_NO             , LSFT(KC_COMMA)  , LSFT(KC_2)        , LSFT(KC_EQUAL)  , KC_NO           ,
        /*L2*/     LSFT(KC_9)          , LSFT(KC_7)        , KC_NO           , LSFT(KC_5)        , KC_NO           , KC_NO           ,
        /*L3*/     KC_BSLS             , LSFT(KC_8)        , KC_NO           , KC_MINUS          , KC_NO           , KC_NO           ,
        /*L4*/     LSFT(KC_SCLN)       , LSFT(KC_GRAVE)    , KC_NO           , LSFT(KC_QUOTE)    , KC_0            , KC_NO           ,

        /*         Down                 Pad                 Up                Nail              Knuckle         DoubleDown*/
        /*RT*/     KC_TRNS             , KC_TRNS           , KC_TRNS         , KC_TRNS          , KC_TRNS       , KC_TRNS         ,
        /*LT*/     KC_TRNS             , KC_TRNS           , KC_TRNS         , KC_TRNS          , KC_TRNS       , KC_TRNS
    ),

    [NAVNUM] = LAYOUT(
        /*         Center            North           East              South           West              Double*/
        /*R1*/     KC_5            , KC_F5           , KC_TRNS         , KC_LEFT       , KC_9            , KC_NO           ,
        /*R2*/     KC_6            , KC_F6           , KC_NO           , KC_RIGHT      , KC_DOT          , KC_NO           ,
        /*R3*/     KC_7            , KC_F7           , KC_NO           , KC_F11        , KC_NO           , KC_NO           ,
        /*R4*/     KC_8            , KC_F8           , KC_NO           , KC_F12        , KC_NO           , KC_NO           ,

        /*L1*/     KC_4            , KC_F4           , KC_0            , KC_UP         , KC_TRNS         , KC_NO           ,
        /*L2*/     KC_3            , KC_F3           , KC_COMMA        , KC_DOWN       , KC_NO           , KC_NO           ,
        /*L3*/     KC_2            , KC_F2           , KC_NO           , KC_F10        , KC_NO           , KC_NO           ,
        /*L4*/     KC_1            , KC_F1           , KC_TRNS         , KC_F9         , KC_NO           , KC_NO           ,

        /*         Down             Pad             Up              Nail            Knuckle         DoubleDown*/
        /*RT*/     TD(SHIFT_CAPS)   , KC_NO         , KC_NO         , KC_BSPC       , LCTL(KC_B)    , KC_NO ,
        /*LT*/     KC_NO            , KC_ESC        , KC_TAB        , KC_DELETE     , KC_LCTL       , TO(DVORAK)
    ),

    [LOL] = LAYOUT(
        /*         Center            North           East              South           West              Double*/
        /*R1*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*R2*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*R3*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*R4*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,

        /*L1*/     KC_P            , KC_4            , KC_Y            , KC_8          , KC_NO           , KC_NO           ,
        /*L2*/     KC_DOT          , KC_3            , KC_U            , KC_6          , KC_E            , KC_NO           ,
        /*L3*/     KC_COMMA        , KC_2            , KC_NO           , KC_S          , KC_O            , KC_NO           ,
        /*L4*/     KC_SCLN         , KC_1            , KC_QUOTE        , KC_A          , KC_TAB          , KC_NO           ,

        /*         Down              Pad             Up                Nail            Knuckle           DoubleDown*/
        /*RT*/     KC_NO           , KC_NO           , TO(DVORAK)      , KC_NO         , KC_NO           , KC_NO           ,
        /*LT*/     LT(FKEYS, KC_SPACE), KC_R         , KC_LALT         , KC_B          , KC_LCTL         , KC_NO
    ),

    [FKEYS] = LAYOUT(
        KC_NO , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,

        KC_F4 , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_F3 , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_F2 , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_F1 , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,

        KC_NO , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO , KC_NO, KC_NO, KC_NO, KC_NO, KC_NO
    ),

    [UTILS] = LAYOUT(
        /*         Center            North           East              South           West              Double*/
        /*R1*/     KC_MSTP         , KC_NO           , KC_NO           , KC_BRIU       , KC_NO           , KC_NO           ,
        /*R2*/     KC_MPLY         , KC_NO           , KC_NO           , KC_BRID       , KC_NO           , KC_NO           ,
        /*R3*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*R4*/     KC_NO           , KC_NO           , KC_TRNS         , KC_NO         , KC_NO           , KC_NO           ,

        /*L1*/     KC_MUTE         , KC_MNXT         , KC_NO           , KC_VOLU       , KC_NO           , KC_NO           ,
        /*L2*/     KC_PSCR         , KC_MPRV         , KC_NO           , KC_VOLD       , KC_NO           , KC_NO           ,
        /*L3*/     KC_SCRL         , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,
        /*L4*/     KC_NO           , KC_NO           , KC_NO           , KC_NO         , KC_NO           , KC_NO           ,

        /*         Down              Pad             Up                Nail            Knuckle           DoubleDown*/
        /*RT*/     KC_TRNS         , KC_TRNS         , KC_TRNS         , KC_TRNS       , KC_TRNS         , KC_TRNS         ,
        /*LT*/     KC_TRNS         , KC_TRNS         , KC_TRNS         , KC_TRNS       , KC_TRNS         , KC_TRNS
    ),

    [MBO] = LAYOUT(
        /*         Center                  North               East            South               West              Double*/
        /*R1*/     KC_BTN1               , KC_NO             , KC_TRNS       , SV_SNIPER_5       , KC_TRNS         , KC_NO           ,
        /*R2*/     KC_BTN3               , SV_RIGHT_DPI_INC  , KC_TRNS       , SV_RIGHT_DPI_DEC  , KC_TRNS         , KC_NO           ,
        /*R3*/     KC_BTN2               , KC_TRNS           , KC_TRNS       , KC_TRNS           , KC_TRNS         , KC_NO           ,
        /*R4*/     SV_RECALIBRATE_POINTER, KC_TRNS           , KC_TRNS       , KC_TRNS           , KC_TRNS         , KC_NO           ,

        /*L1*/     KC_BTN1               , KC_NO             , KC_TRNS       , SV_SNIPER_3       , KC_TRNS         , KC_NO           ,
        /*L2*/     KC_BTN2               , SV_LEFT_DPI_INC   , KC_TRNS       , SV_LEFT_DPI_DEC   , KC_TRNS         , KC_NO           ,
        /*L3*/     KC_BTN3               , KC_TRNS           , KC_TRNS       , KC_TRNS           , KC_TRNS         , KC_NO           ,
        /*L4*/     SV_RECALIBRATE_POINTER, KC_TRNS           , KC_TRNS       , KC_TRNS           , KC_TRNS         , KC_NO           ,

        /*         Down              Pad             Up                Nail            Knuckle           DoubleDown*/
        /*RT*/     KC_TRNS               , KC_TRNS           , KC_TRNS       , KC_NO             , KC_TRNS         , KC_TRNS         ,
        /*LT*/     KC_BTN1               , KC_TRNS           , KC_TRNS       , KC_TRNS           , KC_TRNS         , TO(DVORAK)
    ),
};
// clang-format on
