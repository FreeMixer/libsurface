// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The keys of the Control|24, named: the device facts of the DigiNet module (libsurface#8).
 *
 * A section key is SURFACE_DIGINET_KEY(button, zone), zone 0x18..0x1D, as the desk sends it. A strip button is
 * SURFACE_DIGINET_KEY(button, strip) with the SURFACE_C24_STRIP_* code below. surface_c24_keys[] lists every key with
 * its name, its panel section, whether it has an LED, and whether its code is measured on a desk (Control|24,
 * firmware 1.08) or taken from the source.
 *
 * Source of the codes, names and LED flags not measured here: ReaControl24, control24map.py
 * (github.com/phunkyg/ReaControl24, branch Release, commit 2a963e9, GPL-3.0). The facts are taken; none of its code.
 * The names are that source's, as it spells them. */
#ifndef SURFACE_DIGINET_KEYS_H
#define SURFACE_DIGINET_KEYS_H

#include <stddef.h>
#include <stdint.h>

#ifndef SURFACE_DIGINET_KEY
#define SURFACE_DIGINET_KEY(button, id) ((uint16_t)(((button) & 0x7f) << 8 | ((id) & 0xff)))
#endif

#define SURFACE_C24_ZONE_FIRST 0x18
#define SURFACE_C24_ZONE_LAST  0x1D

struct surface_c24_key {
	uint8_t button;      /* the key byte */
	uint8_t zone;        /* 0x18..0x1D for a section key; 0 for a strip button (its id is the strip) */
	uint8_t led;         /* 1: the key lights (SURFACE_DIGINET_KEY of it, to surface_led()) */
	uint8_t measured;    /* 1: the code is measured on a desk; 0: from the source above */
	const char *name;    /* the source's name */
	const char *section; /* the panel section */
};

/* Every key, section keys by zone and button, then the strip buttons by code. */
extern const struct surface_c24_key surface_c24_keys[];
extern const size_t surface_c24_nkeys;

/* The entry of a decoded key (SURFACE_DIGINET_KEY of a section key, or of a strip button on any strip), or NULL for
 * a key the table does not name. */
const struct surface_c24_key *surface_c24_key_find(uint16_t key);

/* ---- strip buttons: SURFACE_DIGINET_KEY(code, strip), strip 0..23 ---- */
#define SURFACE_C24_STRIP_REC_ARM                        0x00
#define SURFACE_C24_STRIP_PAN_SEND                       0x01
#define SURFACE_C24_STRIP_EQ                             0x02
#define SURFACE_C24_STRIP_DYNAMICS                       0x03
#define SURFACE_C24_STRIP_SWITCH_ACTIVE                  0x04
#define SURFACE_C24_STRIP_AUTO                           0x05
#define SURFACE_C24_STRIP_SELECT                         0x06
#define SURFACE_C24_STRIP_SOLO                           0x07
#define SURFACE_C24_STRIP_MUTE                           0x08
#define SURFACE_C24_STRIP_TOUCH                          0x09
#define SURFACE_C24_STRIP_INSERTS                        0x0A
#define SURFACE_C24_STRIP_PEAK                           0x0B
#define SURFACE_C24_STRIP_SOURCE_TOGGLE                  0x0C
#define SURFACE_C24_STRIP_ROLL_OFF                       0x0D

/* ---- zone 0x18 ---- */
#define SURFACE_C24_UTILITY_F1                           SURFACE_DIGINET_KEY(0x00, 0x18)
#define SURFACE_C24_MISC_MASTER_REC                      SURFACE_DIGINET_KEY(0x01, 0x18)
#define SURFACE_C24_UTILITY_F2                           SURFACE_DIGINET_KEY(0x02, 0x18)
#define SURFACE_C24_MISC_INS_BYPASS                      SURFACE_DIGINET_KEY(0x03, 0x18)
#define SURFACE_C24_UTILITY_F3                           SURFACE_DIGINET_KEY(0x04, 0x18)
#define SURFACE_C24_MISC_EDIT_BYPASS                     SURFACE_DIGINET_KEY(0x05, 0x18)
#define SURFACE_C24_UTILITY_F4                           SURFACE_DIGINET_KEY(0x06, 0x18)
#define SURFACE_C24_MISC_DEFAULT                         SURFACE_DIGINET_KEY(0x07, 0x18)
#define SURFACE_C24_UTILITY_F5                           SURFACE_DIGINET_KEY(0x08, 0x18)
#define SURFACE_C24_METER_SELECT_CHANNEL_TOGGLE          SURFACE_DIGINET_KEY(0x09, 0x18)
#define SURFACE_C24_METER_SELECT_INPUT                   SURFACE_DIGINET_KEY(0x0A, 0x18)
#define SURFACE_C24_METER_SELECT_PRE_FADER               SURFACE_DIGINET_KEY(0x0B, 0x18)
#define SURFACE_C24_METER_SELECT_POST_FADER              SURFACE_DIGINET_KEY(0x0C, 0x18)
#define SURFACE_C24_MISC_SEND_MUTE                       SURFACE_DIGINET_KEY(0x0D, 0x18)
#define SURFACE_C24_METER_SELECT_CLEAR_PEAKS             SURFACE_DIGINET_KEY(0x0E, 0x18)
#define SURFACE_C24_MISC_REC_SAFE                        SURFACE_DIGINET_KEY(0x0F, 0x18)
#define SURFACE_C24_MISC_SHOW_VALUES                     SURFACE_DIGINET_KEY(0x10, 0x18)
#define SURFACE_C24_MISC_SHOW_GROUP                      SURFACE_DIGINET_KEY(0x11, 0x18)
#define SURFACE_C24_MISC_SHOW_CHANNEL_NAMES              SURFACE_DIGINET_KEY(0x12, 0x18)
#define SURFACE_C24_MISC_SHOW_INFO                       SURFACE_DIGINET_KEY(0x13, 0x18)
#define SURFACE_C24_UTILITY_SOLO_CLEAR                   SURFACE_DIGINET_KEY(0x14, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_AUTO_SUSPEND       SURFACE_DIGINET_KEY(0x15, 0x18)
#define SURFACE_C24_MISC_VEL_SENS_ENCODERS               SURFACE_DIGINET_KEY(0x16, 0x18)
#define SURFACE_C24_UTILITY_AUTOMATION_MODE_WRITE        SURFACE_DIGINET_KEY(0x17, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_FADER              SURFACE_DIGINET_KEY(0x18, 0x18)
#define SURFACE_C24_UTILITY_AUTOMATION_MODE_TOUCH        SURFACE_DIGINET_KEY(0x19, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_PAN                SURFACE_DIGINET_KEY(0x1A, 0x18)
#define SURFACE_C24_UTILITY_AUTOMATION_MODE_LATCH        SURFACE_DIGINET_KEY(0x1B, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_MUTE               SURFACE_DIGINET_KEY(0x1C, 0x18)
#define SURFACE_C24_UTILITY_AUTOMATION_MODE_TRIM         SURFACE_DIGINET_KEY(0x1D, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_SEND_LEVEL         SURFACE_DIGINET_KEY(0x1E, 0x18)
#define SURFACE_C24_UTILITY_AUTOMATION_MODE_READ         SURFACE_DIGINET_KEY(0x1F, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_SEND_MUTE          SURFACE_DIGINET_KEY(0x20, 0x18)
#define SURFACE_C24_AUTOMATION_MODE_OFF                  SURFACE_DIGINET_KEY(0x21, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_PLUGIN             SURFACE_DIGINET_KEY(0x22, 0x18)
#define SURFACE_C24_MODIFIERS_SHIFT                      SURFACE_DIGINET_KEY(0x23, 0x18)
#define SURFACE_C24_MODIFIERS_OPTION                     SURFACE_DIGINET_KEY(0x24, 0x18)
#define SURFACE_C24_MODIFIERS_CONTROL                    SURFACE_DIGINET_KEY(0x25, 0x18)
#define SURFACE_C24_MODIFIERS_COMMAND                    SURFACE_DIGINET_KEY(0x26, 0x18)
#define SURFACE_C24_MISC_AUTO_SELECT                     SURFACE_DIGINET_KEY(0x27, 0x18)
#define SURFACE_C24_MISC_PRE_POST                        SURFACE_DIGINET_KEY(0x28, 0x18)
#define SURFACE_C24_MISC_APPLY_TO_ALL_CHANNELS           SURFACE_DIGINET_KEY(0x29, 0x18)
#define SURFACE_C24_MISC_APPLY_TO_ALL_SELECTED_CHANNELS  SURFACE_DIGINET_KEY(0x2A, 0x18)
#define SURFACE_C24_MISC_COPY_SETTINGS_FROM_CHANNEL      SURFACE_DIGINET_KEY(0x2B, 0x18)
#define SURFACE_C24_MISC_SPECIFY_SETTINGS                SURFACE_DIGINET_KEY(0x2C, 0x18)
#define SURFACE_C24_MISC_PASTE_SETTING_TO_CHANNEL        SURFACE_DIGINET_KEY(0x2D, 0x18)
#define SURFACE_C24_UTILITY_SOLO_SAFE                    SURFACE_DIGINET_KEY(0x2E, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_WRITE_AUTO_TO_START SURFACE_DIGINET_KEY(0x2F, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_WRITE_AUTO_TO_ALL  SURFACE_DIGINET_KEY(0x30, 0x18)
#define SURFACE_C24_AUTOMATION_ENABLE_WRITE_AUTO_TO_END  SURFACE_DIGINET_KEY(0x31, 0x18)
#define SURFACE_C24_UTILITY_F6                           SURFACE_DIGINET_KEY(0x32, 0x18)
#define SURFACE_C24_UTILITY_F7                           SURFACE_DIGINET_KEY(0x33, 0x18)
#define SURFACE_C24_UTILITY_F8                           SURFACE_DIGINET_KEY(0x34, 0x18)
#define SURFACE_C24_UTILITY_F9                           SURFACE_DIGINET_KEY(0x35, 0x18)
#define SURFACE_C24_UTILITY_F10                          SURFACE_DIGINET_KEY(0x36, 0x18)

/* ---- zone 0x19 ---- */
#define SURFACE_C24_WINDOW_MIX                           SURFACE_DIGINET_KEY(0x00, 0x19)
#define SURFACE_C24_WINDOW_EDIT_BYPASS                   SURFACE_DIGINET_KEY(0x01, 0x19)
#define SURFACE_C24_WINDOW_STATUS                        SURFACE_DIGINET_KEY(0x02, 0x19)
#define SURFACE_C24_WINDOW_TRANS                         SURFACE_DIGINET_KEY(0x03, 0x19)
#define SURFACE_C24_WINDOW_PAN                           SURFACE_DIGINET_KEY(0x04, 0x19)
#define SURFACE_C24_WINDOW_ALT                           SURFACE_DIGINET_KEY(0x05, 0x19)
#define SURFACE_C24_WINDOW_PLUG_IN                       SURFACE_DIGINET_KEY(0x06, 0x19)
#define SURFACE_C24_WINDOW_MEM_LOC                       SURFACE_DIGINET_KEY(0x07, 0x19)
#define SURFACE_C24_ZOOM_PRESETS_PRESET1                 SURFACE_DIGINET_KEY(0x08, 0x19)
#define SURFACE_C24_ZOOM_PRESETS_PRESET2                 SURFACE_DIGINET_KEY(0x09, 0x19)
#define SURFACE_C24_ZOOM_PRESETS_PRESET3                 SURFACE_DIGINET_KEY(0x0A, 0x19)
#define SURFACE_C24_ZOOM_PRESETS_PRESET4                 SURFACE_DIGINET_KEY(0x0B, 0x19)
#define SURFACE_C24_NAVIGATION_NAV                       SURFACE_DIGINET_KEY(0x0C, 0x19)
#define SURFACE_C24_NAVIGATION_ZOOM                      SURFACE_DIGINET_KEY(0x0D, 0x19)
#define SURFACE_C24_NAVIGATION_SEL_ADJ                   SURFACE_DIGINET_KEY(0x0E, 0x19)
#define SURFACE_C24_NAVIGATION_CURSOR_UP                 SURFACE_DIGINET_KEY(0x0F, 0x19)
#define SURFACE_C24_NAVIGATION_CURSOR_LEFT               SURFACE_DIGINET_KEY(0x10, 0x19)
#define SURFACE_C24_NAVIGATION_CURSOR_RIGHT              SURFACE_DIGINET_KEY(0x11, 0x19)
#define SURFACE_C24_NAVIGATION_CURSOR_DOWN               SURFACE_DIGINET_KEY(0x12, 0x19)
#define SURFACE_C24_COUNTER_MODE                         SURFACE_DIGINET_KEY(0x13, 0x19)
#define SURFACE_C24_ZOOM_PRESETS_PRESET5                 SURFACE_DIGINET_KEY(0x14, 0x19)

/* ---- zone 0x1A ---- */
#define SURFACE_C24_KEYPAD_N0                            SURFACE_DIGINET_KEY(0x00, 0x1A)
#define SURFACE_C24_KEYPAD_N1                            SURFACE_DIGINET_KEY(0x01, 0x1A)
#define SURFACE_C24_KEYPAD_N2                            SURFACE_DIGINET_KEY(0x02, 0x1A)
#define SURFACE_C24_KEYPAD_N3                            SURFACE_DIGINET_KEY(0x03, 0x1A)
#define SURFACE_C24_KEYPAD_N4                            SURFACE_DIGINET_KEY(0x04, 0x1A)
#define SURFACE_C24_KEYPAD_N5                            SURFACE_DIGINET_KEY(0x05, 0x1A)
#define SURFACE_C24_KEYPAD_N6                            SURFACE_DIGINET_KEY(0x06, 0x1A)
#define SURFACE_C24_KEYPAD_N7                            SURFACE_DIGINET_KEY(0x07, 0x1A)
#define SURFACE_C24_KEYPAD_N8                            SURFACE_DIGINET_KEY(0x08, 0x1A)
#define SURFACE_C24_KEYPAD_N9                            SURFACE_DIGINET_KEY(0x09, 0x1A)
#define SURFACE_C24_KEYPAD_CLEAR                         SURFACE_DIGINET_KEY(0x0A, 0x1A)
#define SURFACE_C24_KEYPAD_EQUALS                        SURFACE_DIGINET_KEY(0x0B, 0x1A)
#define SURFACE_C24_KEYPAD_DIVIDE                        SURFACE_DIGINET_KEY(0x0C, 0x1A)
#define SURFACE_C24_KEYPAD_MULTIPLY                      SURFACE_DIGINET_KEY(0x0D, 0x1A)
#define SURFACE_C24_KEYPAD_MINUS                         SURFACE_DIGINET_KEY(0x0E, 0x1A)
#define SURFACE_C24_KEYPAD_PLUS                          SURFACE_DIGINET_KEY(0x0F, 0x1A)
#define SURFACE_C24_KEYPAD_POINT                         SURFACE_DIGINET_KEY(0x10, 0x1A)
#define SURFACE_C24_KEYPAD_ENTER                         SURFACE_DIGINET_KEY(0x11, 0x1A)

/* ---- zone 0x1B ---- */
#define SURFACE_C24_EDIT_MODE_SHUFFLE                    SURFACE_DIGINET_KEY(0x00, 0x1B)
#define SURFACE_C24_EDIT_MODE_SLIP                       SURFACE_DIGINET_KEY(0x01, 0x1B)
#define SURFACE_C24_EDIT_MODE_SPOT                       SURFACE_DIGINET_KEY(0x02, 0x1B)
#define SURFACE_C24_EDIT_MODE_GRID                       SURFACE_DIGINET_KEY(0x03, 0x1B)
#define SURFACE_C24_EDIT_FUNCTION_CUT                    SURFACE_DIGINET_KEY(0x04, 0x1B)
#define SURFACE_C24_EDIT_FUNCTION_COPY                   SURFACE_DIGINET_KEY(0x05, 0x1B)
#define SURFACE_C24_EDIT_FUNCTION_PASTE                  SURFACE_DIGINET_KEY(0x06, 0x1B)
#define SURFACE_C24_EDIT_FUNCTION_DELETE                 SURFACE_DIGINET_KEY(0x07, 0x1B)
#define SURFACE_C24_EDIT_FUNCTION_SEPARATE               SURFACE_DIGINET_KEY(0x08, 0x1B)
#define SURFACE_C24_EDIT_FUNCTION_CAPTURE                SURFACE_DIGINET_KEY(0x09, 0x1B)
#define SURFACE_C24_BANK_LEFT                            SURFACE_DIGINET_KEY(0x0A, 0x1B)
#define SURFACE_C24_BANK_NUDGE                           SURFACE_DIGINET_KEY(0x0B, 0x1B)
#define SURFACE_C24_BANK_RIGHT                           SURFACE_DIGINET_KEY(0x0C, 0x1B)
#define SURFACE_C24_EDIT_TOOLS_TRIM                      SURFACE_DIGINET_KEY(0x0D, 0x1B)
#define SURFACE_C24_EDIT_TOOLS_SELECT                    SURFACE_DIGINET_KEY(0x0E, 0x1B)
#define SURFACE_C24_EDIT_TOOLS_GRAB                      SURFACE_DIGINET_KEY(0x0F, 0x1B)
#define SURFACE_C24_EDIT_TOOLS_PENCIL                    SURFACE_DIGINET_KEY(0x10, 0x1B)
#define SURFACE_C24_EDIT_TOOLS_MIDI_TOOLS                SURFACE_DIGINET_KEY(0x11, 0x1B)
#define SURFACE_C24_EDIT_TOOLS_SMART                     SURFACE_DIGINET_KEY(0x12, 0x1B)
#define SURFACE_C24_FADERS_MASTER_FADERS                 SURFACE_DIGINET_KEY(0x13, 0x1B)
#define SURFACE_C24_UTILITY_UNDO                         SURFACE_DIGINET_KEY(0x14, 0x1B)
#define SURFACE_C24_UTILITY_SAVE                         SURFACE_DIGINET_KEY(0x15, 0x1B)
#define SURFACE_C24_GROUPS_CREATE                        SURFACE_DIGINET_KEY(0x17, 0x1B)
#define SURFACE_C24_GROUPS_ENABLE                        SURFACE_DIGINET_KEY(0x18, 0x1B)
#define SURFACE_C24_GROUPS_EDIT_BYPASS                   SURFACE_DIGINET_KEY(0x19, 0x1B)
#define SURFACE_C24_GROUPS_DELETE                        SURFACE_DIGINET_KEY(0x1A, 0x1B)
#define SURFACE_C24_GROUPS_SUSPEND                       SURFACE_DIGINET_KEY(0x1B, 0x1B)
#define SURFACE_C24_EDIT_FUNCTION_DUPLICATE              SURFACE_DIGINET_KEY(0x1C, 0x1B)
#define SURFACE_C24_EDIT_FUNCTION_REPEAT                 SURFACE_DIGINET_KEY(0x1D, 0x1B)
#define SURFACE_C24_EDIT_TOOLS_LINK_EDIT_TL              SURFACE_DIGINET_KEY(0x1E, 0x1B)
#define SURFACE_C24_EDIT_TOOLS_AUTO_TO_CUR               SURFACE_DIGINET_KEY(0x1F, 0x1B)
#define SURFACE_C24_EDIT_TOOLS_AUTO_TO_ALL               SURFACE_DIGINET_KEY(0x20, 0x1B)
#define SURFACE_C24_UTILITY_ESC_CANCEL                   SURFACE_DIGINET_KEY(0x21, 0x1B)

/* ---- zone 0x1C ---- */
#define SURFACE_C24_TRANSPORT_AUDITION                   SURFACE_DIGINET_KEY(0x00, 0x1C)
#define SURFACE_C24_TRANSPORT_PRE_ROLL                   SURFACE_DIGINET_KEY(0x01, 0x1C)
#define SURFACE_C24_TRANSPORT_IN                         SURFACE_DIGINET_KEY(0x02, 0x1C)
#define SURFACE_C24_TRANSPORT_OUT                        SURFACE_DIGINET_KEY(0x03, 0x1C)
#define SURFACE_C24_TRANSPORT_POST_ROLL                  SURFACE_DIGINET_KEY(0x04, 0x1C)
#define SURFACE_C24_TRANSPORT_GO_TO_START                SURFACE_DIGINET_KEY(0x05, 0x1C)
#define SURFACE_C24_TRANSPORT_GO_TO_END                  SURFACE_DIGINET_KEY(0x06, 0x1C)
#define SURFACE_C24_TRANSPORT_ONLINE                     SURFACE_DIGINET_KEY(0x07, 0x1C)
#define SURFACE_C24_TRANSPORT_EXT_TRANS                  SURFACE_DIGINET_KEY(0x08, 0x1C)
#define SURFACE_C24_TRANSPORT_LOOP_PLAY                  SURFACE_DIGINET_KEY(0x09, 0x1C)
#define SURFACE_C24_TRANSPORT_LOOP_RECORD                SURFACE_DIGINET_KEY(0x0A, 0x1C)
#define SURFACE_C24_TRANSPORT_QUICK_PUNCH                SURFACE_DIGINET_KEY(0x0B, 0x1C)
#define SURFACE_C24_UTILITY_TALKBACK                     SURFACE_DIGINET_KEY(0x0C, 0x1C)
#define SURFACE_C24_TRANSPORT_REWIND                     SURFACE_DIGINET_KEY(0x0D, 0x1C)
#define SURFACE_C24_TRANSPORT_FORWARD                    SURFACE_DIGINET_KEY(0x0E, 0x1C)
#define SURFACE_C24_TRANSPORT_STOP                       SURFACE_DIGINET_KEY(0x0F, 0x1C)
#define SURFACE_C24_TRANSPORT_PLAY                       SURFACE_DIGINET_KEY(0x10, 0x1C)
#define SURFACE_C24_TRANSPORT_RECORD                     SURFACE_DIGINET_KEY(0x11, 0x1C)
#define SURFACE_C24_TRANSPORT_SCRUB                      SURFACE_DIGINET_KEY(0x12, 0x1C)
#define SURFACE_C24_TRANSPORT_SHUTTLE                    SURFACE_DIGINET_KEY(0x13, 0x1C)

/* ---- zone 0x1D ---- */
#define SURFACE_C24_MONITOR_PHASE_REMOTE_PRE_MONITOR_PHASE SURFACE_DIGINET_KEY(0x00, 0x1D)
#define SURFACE_C24_MONITOR_PHASE_REMOTE_PRE_REMOTE_MIC_PRE SURFACE_DIGINET_KEY(0x01, 0x1D)
#define SURFACE_C24_INSERTS_COMPARE                      SURFACE_DIGINET_KEY(0x02, 0x1D)
#define SURFACE_C24_INSERTS_MASTER_BYPASS                SURFACE_DIGINET_KEY(0x03, 0x1D)
#define SURFACE_C24_INSERTS_PARAM                        SURFACE_DIGINET_KEY(0x04, 0x1D)
#define SURFACE_C24_ASSIGNMENT_INPUT                     SURFACE_DIGINET_KEY(0x05, 0x1D)
#define SURFACE_C24_ASSIGNMENT_OUTPUT                    SURFACE_DIGINET_KEY(0x06, 0x1D)
#define SURFACE_C24_ASSIGNMENT_ASSIGN                    SURFACE_DIGINET_KEY(0x07, 0x1D)
#define SURFACE_C24_ASSIGNMENT_ESC_CANCEL                SURFACE_DIGINET_KEY(0x08, 0x1D)
#define SURFACE_C24_INSERTS_PLUG_IN_SAFE                 SURFACE_DIGINET_KEY(0x09, 0x1D)
#define SURFACE_C24_SENDS_FLIP                           SURFACE_DIGINET_KEY(0x0A, 0x1D)
#define SURFACE_C24_SENDS_AF                             SURFACE_DIGINET_KEY(0x0B, 0x1D)
#define SURFACE_C24_SENDS_BG                             SURFACE_DIGINET_KEY(0x0C, 0x1D)
#define SURFACE_C24_SENDS_CH                             SURFACE_DIGINET_KEY(0x0D, 0x1D)
#define SURFACE_C24_SENDS_DI                             SURFACE_DIGINET_KEY(0x0E, 0x1D)
#define SURFACE_C24_SENDS_EJ                             SURFACE_DIGINET_KEY(0x0F, 0x1D)
#define SURFACE_C24_PANS_LCR_FRONT_DIV                   SURFACE_DIGINET_KEY(0x10, 0x1D)
#define SURFACE_C24_PANS_REAR_REAR_DIV                   SURFACE_DIGINET_KEY(0x11, 0x1D)
#define SURFACE_C24_PANS_FR_FR_DIV                       SURFACE_DIGINET_KEY(0x12, 0x1D)
#define SURFACE_C24_PANS_CENT                            SURFACE_DIGINET_KEY(0x13, 0x1D)
#define SURFACE_C24_CHANNEL_BAR_SCROLL_LEFT              SURFACE_DIGINET_KEY(0x14, 0x1D)
#define SURFACE_C24_CHANNEL_BAR_SCROLL_RIGHT             SURFACE_DIGINET_KEY(0x15, 0x1D)
#define SURFACE_C24_PANS_LFE                             SURFACE_DIGINET_KEY(0x16, 0x1D)
#define SURFACE_C24_PANS_LEFT_RIGHT                      SURFACE_DIGINET_KEY(0x17, 0x1D)

#endif
