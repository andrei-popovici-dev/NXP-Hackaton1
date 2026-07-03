// SPDX-License-Identifier: GPL-2.0
/*
 * menu - LVGL warm-up sample: multiple screens, button navigation
 *
 * Three full-size "screens" (Ping Pong / Tetris / Snake); the hackpad
 * flips between them and launches the selected one:
 *   SW2 = next screen, SW4 = previous screen
 *   SW1 = UP = launch the currently shown game
 *
 * "Navigation" is just hiding all screens except one with
 * LV_OBJ_FLAG_HIDDEN. No allocation, no teardown, instant switching.
 *
 * Needs st7789fb.ko and hackpad.ko.
 *
 * API used:
 *   lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)      hide a screen
 *   lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN)   show a screen
 *   hal_button_pressed(btn)                       edge-triggered press
 *   (cur + 1) % N and (cur + N - 1) % N           wrap-around index
 */
#include "hal.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#define NUM_SCREENS 3
// pong tetris snake
static const char *names[NUM_SCREENS] = { "Ping Pong", "Tetris", "Snake" };
static const uint32_t tints[NUM_SCREENS] = { 0x103020, 0x102030, 0x301020 };

/* Game executables, one per screen. Edit these to your real binaries.
 * If your games are in-process functions instead, replace the body of
 * launch_game() with a direct call. */
static const char *bins[NUM_SCREENS] = {
	"/root/pong",
	"/root/tetris",
	"/root/snake",
};

/* NXP letter colours: N = yellow, X = blue, P = green (official brand hex) */
#define NXP_YELLOW 0xFCB316
#define NXP_BLUE   0x6DACDE
#define NXP_GREEN  0x6D9A45   /* livelier light-green alternative: 0xBFD730 */

/* Use a big logo font if the build has it, otherwise fall back gracefully. */
#if LV_FONT_MONTSERRAT_48
#define NXP_LOGO_FONT lv_font_montserrat_48
#define NXP_SUB_FONT  lv_font_montserrat_24
#else
#define NXP_LOGO_FONT lv_font_montserrat_24
#define NXP_SUB_FONT  lv_font_montserrat_12
#endif

static lv_obj_t *screens[NUM_SCREENS];
static lv_obj_t *splash;
static lv_obj_t *temp_label;    /* top-left  - update from your temp fetch */
static lv_obj_t *clock_label;   /* top-right - 24h hh:mm, driven by clock_tick */
static int cur;
static int splash_active = 1;   /* swallow button presses during the splash */

static void show_screen(int idx)
{
	for (int i = 0; i < NUM_SCREENS; i++) {
		if (i == idx)
			lv_obj_remove_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
		else
			lv_obj_add_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
	}
	cur = idx;
}

static void launch_game(int idx)
{
	/* Hand the framebuffer + hackpad over to the game binary, block the
	 * menu while it runs, then come back when the player quits. */
	pid_t pid = fork();

	if (pid == 0) {
		execl(bins[idx], bins[idx], (char *)NULL);
		_exit(127);                    /* only reached if exec fails */
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);         /* pause the menu during play */

	/* the game scribbled all over the framebuffer - force a full repaint */
	lv_obj_invalidate(lv_screen_active());
}

static void clock_tick(lv_timer_t *t)
{
	LV_UNUSED(t);

	time_t now = time(NULL);
	now += (time_t)1783093800;
	struct tm tm;
	char buf[6];                       /* "hh:mm" + NUL */

	localtime_r(&now, &tm);
	strftime(buf, sizeof(buf), "%H:%M", &tm);
	lv_label_set_text(clock_label, buf);
}

static void nav_tick(lv_timer_t *t)
{
	LV_UNUSED(t);

	if (splash_active)
		return;

	if (hal_button_pressed(HACKPAD_BTN_SW2))
		show_screen((cur + 1) % NUM_SCREENS);
	if (hal_button_pressed(HACKPAD_BTN_SW4))
		show_screen((cur + NUM_SCREENS - 1) % NUM_SCREENS);
	if (hal_button_pressed(HACKPAD_BTN_SW3))   /* UP = launch */
		launch_game(cur);
}

static void splash_finished(lv_timer_t *t)
{
	LV_UNUSED(t);
	lv_obj_delete(splash);
	splash = NULL;
	splash_active = 0;
}

static void build_splash(lv_obj_t *parent)
{
	static const uint32_t nxp_cols[3] = { NXP_YELLOW, NXP_BLUE, NXP_GREEN };
	static const char *nxp_ch[3] = { "N", "X", "P" };

	splash = lv_obj_create(parent);
	lv_obj_set_size(splash, 240, 240);
	lv_obj_set_pos(splash, 0, 0);
	lv_obj_set_style_bg_color(splash, lv_color_black(), 0);
	lv_obj_set_style_border_width(splash, 0, 0);
	lv_obj_set_style_radius(splash, 0, 0);
	lv_obj_set_style_pad_all(splash, 0, 0);
	lv_obj_remove_flag(splash, LV_OBJ_FLAG_SCROLLABLE);

	/* logo + subtitle stacked and centred as one group */
	lv_obj_t *box = lv_obj_create(splash);
	lv_obj_remove_style_all(box);
	lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_row(box, 8, 0);
	lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_center(box);

	/* NXP wordmark: three coloured letters side by side */
	lv_obj_t *row = lv_obj_create(box);
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(row, 2, 0);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

	for (int i = 0; i < 3; i++) {
		lv_obj_t *ch = lv_label_create(row);
		lv_label_set_text(ch, nxp_ch[i]);
		lv_obj_set_style_text_color(ch, lv_color_hex(nxp_cols[i]), 0);
		lv_obj_set_style_text_font(ch, &NXP_LOGO_FONT, 0);
	}

	lv_obj_t *sub = lv_label_create(box);
	lv_label_set_text(sub, "Game Console");
	lv_obj_set_style_text_color(sub, lv_color_white(), 0);
	lv_obj_set_style_text_font(sub, &NXP_SUB_FONT, 0);
}

int main(void)
{
	hal_init();

	double temp_c;
	double press_hpa;
	int rc = hal_bmp280_read(&temp_c, &press_hpa);

	if (rc)
		printf("ERROR: Could not read temperature!\n");

	lv_obj_t *scr = lv_screen_active();
	lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	/* full-screen game panels - no empty header strip */
	for (int i = 0; i < NUM_SCREENS; i++) {
		lv_obj_t *s = lv_obj_create(scr);

		lv_obj_set_size(s, 240, 240);
		lv_obj_set_pos(s, 0, 0);
		lv_obj_set_style_bg_color(s, lv_color_hex(tints[i]), 0);
		lv_obj_set_style_border_width(s, 0, 0);
		lv_obj_set_style_radius(s, 0, 0);
		lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);

		lv_obj_t *lbl = lv_label_create(s);
		lv_label_set_text_fmt(lbl, "%s", names[i]);
		lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
		lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
		lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_center(lbl);

		screens[i] = s;
	}

	lv_obj_t *lhs = lv_label_create(scr);
	lv_label_set_text_fmt(lhs, "<");
	lv_obj_set_style_text_color(lhs, lv_color_white(), 0);
	lv_obj_set_style_text_font(lhs, &lv_font_montserrat_24, 0);
	lv_obj_align(lhs, LV_ALIGN_LEFT_MID, 10, 0);

	lv_obj_t *rhs = lv_label_create(scr);
	lv_label_set_text_fmt(rhs, ">");
	lv_obj_set_style_text_color(rhs, lv_color_white(), 0);
	lv_obj_set_style_text_font(rhs, &lv_font_montserrat_24, 0);
	lv_obj_align(rhs, LV_ALIGN_RIGHT_MID, -10, 0);
	
	/* prompt overlay - created after the panels so it sits on top of them */
	lv_obj_t *prompt = lv_label_create(scr);
	lv_label_set_text_fmt(prompt, "Press UP to start...");
	lv_obj_set_style_text_color(prompt, lv_color_white(), 0);
	lv_obj_set_style_text_font(prompt, &lv_font_montserrat_12, 0);
	lv_obj_align(prompt, LV_ALIGN_BOTTOM_MID, 0, -6);

	/* top-left temperature - placeholder until you fetch a real reading:
	 *   lv_label_set_text_fmt(temp_label, "%d\u00B0C", celsius); */
	temp_label = lv_label_create(scr);
	
	if (rc == 0) {
		lv_label_set_text_fmt(temp_label, "%f\u00B0C", temp_c);
	} else {
		lv_label_set_text(temp_label, "--\u00B0C");
	}

	lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_12, 0);
	lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 4, 4);

	/* top-right clock - 24h hh:mm, refreshed once a second */
	clock_label = lv_label_create(scr);
	lv_obj_set_style_text_color(clock_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_12, 0);
	lv_obj_align(clock_label, LV_ALIGN_TOP_RIGHT, -4, 4);
	clock_tick(NULL);                   /* seed before the first tick fires */

	show_screen(0);

	build_splash(scr);
	lv_timer_t *st = lv_timer_create(splash_finished, 3000, NULL);
	lv_timer_set_repeat_count(st, 1);

	lv_timer_create(nav_tick, 20, NULL);
	lv_timer_create(clock_tick, 1000, NULL);

	hal_run();
	return 0;
}
