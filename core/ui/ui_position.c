// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Settings > Location > Fixed position (docs/spec/position.md 4): the latitude and longitude the position service
// reports when the radio has no GPS fix, or when the source is set to Fixed. For every protocol that sends our
// position (DMR GPS Info, LRRP, APRS).
//
// Latitude and longitude are typed as decimal degrees to 4 places (about 11 m): the digits fill "DD.DDDD" or
// "DDD.DDDD" from the left (a longitude's first digit of 2-9 gets its leading zero by itself), Up / Down (or the knob)
// switch N / S or E / W, BACK takes the last digit back, OK saves (the digits not typed are zeros). Opening the editor
// shows the stored value; the first digit starts over.

#include "config.h"
#if CONFIG_POSITION
#include "ui_nav.h"
#include "ui_svc.h"
#include "fmt.h"
#include "i18n.h"

enum { PP_LAT, PP_LON, PP_ALT, PP_COUNT };
#define POS_DEC		4u		// decimal places typed
#define POS_DIGITS_MAX	7u

static bool pos_editing;
static uint8_t pos_n;			// digits typed (0 = showing the stored value)
static char pos_digits[POS_DIGITS_MAX + 1];
static uint32_t pos_stored;		// the stored value, 1e-4 degree
static bool pos_neg;			// south / west
static int32_t pos_original;

static uint8_t pos_int_digits(uint16_t row) { return row == PP_LAT ? 2u : row == PP_LON ? 3u : 5u; }
static uint8_t pos_dec_digits(uint16_t row) { return row == PP_ALT ? 0u : POS_DEC; }
static uint32_t pos_max(uint16_t row) { return row == PP_LAT ? 900000u : row == PP_LON ? 1800000u : 32767u; }

static uint8_t pos_id(uint16_t row)
{
	return row == PP_LAT ? UI_SET_POS_FIXED_LAT : row == PP_LON ? UI_SET_POS_FIXED_LON : UI_SET_POS_FIXED_ALT;
}

// 1e-7 degree to the 1e-4 degree typed, without the sign
static uint32_t pos_e4(int32_t e7) { return ((e7 < 0 ? 0u - (uint32_t)e7 : (uint32_t)e7) + 500u) / 1000u; }

// The typed digits as 1e-4 degree, the ones not typed yet as zeros
static uint32_t pos_typed(uint16_t row)
{
	uint32_t v = 0;
	uint8_t total = row == PP_ALT ? pos_n : (uint8_t)(pos_int_digits(row) + pos_dec_digits(row));
	for (uint8_t k = 0; k < total; k++) {
		v = v * 10u + (k < pos_n ? (uint32_t)(pos_digits[k] - '0') : 0u);
	}
	return v;
}

// "33.7490 N", "84.3880 W"; the editor shows every place ("084.3880 W"), the ones still to type as underscores
// ("33.7___ N")
static void pos_text(sbuf_t *b, uint16_t row, uint32_t v, bool neg, bool editing, bool typing)
{
	uint8_t ints = pos_int_digits(row);
	uint8_t decimals = pos_dec_digits(row);
	char d[POS_DIGITS_MAX + 1];
	for (int k = ints + (int)decimals - 1; k >= 0; k--, v /= 10u) {
		d[k] = (char)('0' + v % 10u);
	}
	if (row == PP_ALT) {
		if (!editing) {
			uint8_t k = 0;
			while (k + 1u < ints && d[k] == '0')
				k++;
			sb_chr(b, neg ? '-' : '+');
			for (; k < ints; k++)
				sb_chr(b, d[k]);
		} else {
			sb_chr(b, neg ? '-' : '+');
			for (uint8_t k = 0; k < ints; k++)
				sb_chr(b, typing && k >= pos_n ? '_' : d[k]);
		}
		sb_str(b, " m");
		return;
	}
	uint8_t k = 0;
	while (!editing && k + 1u < ints && d[k] == '0') {
		k++;	// no leading zeros outside the editor
	}
	for (; k < ints + decimals; k++) {
		if (k == ints) {
			sb_chr(b, '.');
		}
		sb_chr(b, typing && k >= pos_n ? '_' : d[k]);
	}
	sb_chr(b, ' ');
	sb_chr(b, row == PP_LAT ? (neg ? 'S' : 'N') : (neg ? 'W' : 'E'));
}

static uint16_t pos_count(void) { return PP_COUNT; }

static void pos_item(uint16_t i, ui_item_t *it)
{
	static const tr_id_t names[] = {
		TRN(POS_LABEL_LATITUDE, "Latitude"), TRN(POS_LABEL_LONGITUDE, "Longitude"),
		TRN(POS_LABEL_ALTITUDE, "Altitude"),
	};
	str_copy(it->label, sizeof it->label, tr(names[i]));
	it->num = (char)('1' + i);
	sbuf_t b;
	sb_init(&b, it->value, sizeof it->value);
	bool edit = pos_editing && i == ui_frame()->focus;
	if (edit) {
		pos_text(&b, i, pos_n != 0 ? pos_typed(i) : pos_stored, pos_neg, true, pos_n != 0);
		it->flags |= UI_ITEM_EDIT;
		return;
	}
	int32_t e7 = ui_svc_setting_get(pos_id(i));
	if (i == PP_ALT) {
		if (e7 == INT16_MIN) {
			sb_str(&b, TR(VALUE_NOT_SET, "Not set"));
			return;
		}
		pos_text(&b, i, e7 < 0 ? 0u - (uint32_t)e7 : (uint32_t)e7, e7 < 0, false, false);
		return;
	}
	if (ui_svc_setting_get(UI_SET_POS_FIXED_LAT) == 0 && ui_svc_setting_get(UI_SET_POS_FIXED_LON) == 0) {
		sb_str(&b, TR(VALUE_NOT_SET, "Not set"));
		return;
	}
	pos_text(&b, i, pos_e4(e7), e7 < 0, false, false);
}

static void pos_open(uint16_t row)
{
	int32_t e7 = ui_svc_setting_get(pos_id(row));
	pos_editing = true;
	pos_n = 0;
	pos_original = e7;
	pos_neg = e7 < 0 && e7 != INT16_MIN;
	pos_stored = row == PP_ALT ? (e7 == INT16_MIN ? 0u : (e7 < 0 ? 0u - (uint32_t)e7 : (uint32_t)e7)) :
		     pos_e4(e7);
}

static void pos_save(uint16_t row)
{
	uint32_t v = pos_n != 0 ? pos_typed(row) : pos_stored;
	int32_t e7 = row == PP_ALT && pos_n == 0 ? pos_original :
		     row == PP_ALT ? (int32_t)v * (pos_neg ? -1 : 1) :
		     (int32_t)(v * 1000u) * (pos_neg ? -1 : 1);
	(void)ui_svc_setting_set(pos_id(row), e7);
	pos_editing = false;
}

static void pos_select(uint16_t i, bool hold)
{
	if (i == PP_ALT && hold && !pos_editing) {
		(void)ui_svc_setting_set(UI_SET_POS_FIXED_ALT, INT16_MIN);
		ui_toast(TR(POS_ALTITUDE_CLEARED, "Altitude cleared"));
		ui_dirty();
		return;
	}
	if (pos_editing) {
		pos_save(i);
	} else {
		pos_open(i);
	}
	ui_dirty();
}

static bool pos_key(const input_event_t *ev)
{
	uint16_t row = ui_frame()->focus;
	if (!pos_editing) {
		return false;
	}
	bool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;
	bool flip = ((ev->action == ACT_NAV_UP || ev->action == ACT_NAV_DOWN) && press) ||
		    (ev->action == ACT_ENC_STEP && ev->gesture != GESTURE_UP);
	if (flip) {
		pos_neg = !pos_neg;
		ui_dirty();
		return true;
	}
	if (ev->action >= ACT_DIGIT_0 && ev->action <= ACT_DIGIT_9 && press) {
		uint8_t total = (uint8_t)(pos_int_digits(row) + pos_dec_digits(row));
		char d = (char)('0' + (ev->action - ACT_DIGIT_0));
		if (row == PP_LON && pos_n == 0 && d >= '2') {
			pos_digits[pos_n++] = '0';	// 2-9 can't lead a longitude (180 at most): 84.3880 is typed 8 4 3 8 8 0
		}
		if (pos_n < total) {
			pos_digits[pos_n++] = d;
			if (pos_typed(row) > pos_max(row)) {
				pos_n--;	// past 90 / 180 degrees: refused as typed
				ui_toast(row == PP_ALT ? TR(POS_TOAST_ALTITUDE, "Altitude must be -32767 to 32767 m") :
					 TR(POS_TOAST_RANGE, "Up to 90 N/S, 180 E/W"));
			}
		}
		ui_dirty();
		return true;
	}
	if (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS) {
		if (pos_n != 0) {
			pos_n--;	// the last digit back
		} else {
			pos_editing = false;	// nothing typed: leave without a change
		}
		ui_dirty();
		return true;
	}
	return false;	// OK: pos_select saves
}

static void pos_enter(void) { pos_editing = false; }

const ui_scr_def_t ui_fixed_pos_screen = {
	.name = "fixed-pos", .flags = SF_LIST | SF_NUMBERED | SF_FIELD,
	.prefix = TRN(SET_LABEL_FIXED_POSITION, "Fixed position"),
	.hint = TRN(HINT_OK_CHANGE_BACK, "OK change  Back"),
	.count = pos_count, .item = pos_item, .select = pos_select, .key = pos_key, .enter = pos_enter,
};
#endif
