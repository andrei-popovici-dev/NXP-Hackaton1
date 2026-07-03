// SPDX-License-Identifier: GPL-2.0
/*
 * pong - retro Pong (PROJECT IMPLEMENTATION)
 */
#include <stdlib.h>
#include "hal.h"

#define SCREEN      240
#define PADDLE_W    6
#define PADDLE_H    40
#define BALL_SIZE   8
#define PADDLE_SPD  4
#define SW_PLAYER_SPD 3
#define WIN_SCORE   5

static lv_obj_t *lpad, *rpad, *ball;
static lv_obj_t *lscore_lbl, *rscore_lbl, *msg_lbl;

static int lpad_y, rpad_y;          /* paddle centers */
static int ball_x, ball_y;          /* ball top-left  */
static int vx, vy;                  /* ball velocity  */
static int lscore, rscore;
static bool running;

static lv_obj_t *make_rect(int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(lv_screen_active());

    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return o;
}

/* Put the ball in the center, moving towards dir (+1 right, -1 left) */
static void serve(int dir)
{
    ball_x = SCREEN / 2 - BALL_SIZE / 2;
    ball_y = SCREEN / 2 - BALL_SIZE / 2;
    vx = 3 * dir;
    vy = (rand() % 2) ? 2 : -2;
}

static void new_game(void)
{
    lscore = rscore = 0;
    lpad_y = rpad_y = SCREEN / 2;
    lv_label_set_text(lscore_lbl, "0");
    lv_label_set_text(rscore_lbl, "0");
    lv_label_set_text(msg_lbl, "");
    hal_leds(0); // Oprește LED-urile la început de joc nou
    serve((rand() % 2) ? 1 : -1);
    running = true;
}

/* Called at 60 fps - the whole game lives here */
static void game_tick(lv_timer_t *t)
{
    LV_UNUSED(t);

    uint32_t btns = hal_buttons();

    if (hal_button_pressed(HACKPAD_BTN_SW2) && !running)
        new_game();

    if (hal_button_pressed(HACKPAD_BTN_SW4))
	exit(0);

    if (!running)
        return;

    /* TODO 1: player paddle.
     * While SW1 is held, decrease lpad_y by PADDLE_SPD; while SW2 is
     * held, increase it. Clamp so the paddle stays on screen.
     */
    if (btns & (1 << HACKPAD_BTN_SW1)) {
        lpad_y += PADDLE_SPD; // goes down
    }
    if (btns & (1 << HACKPAD_BTN_SW3)) {
        lpad_y -= PADDLE_SPD; // goes up
    }
    lpad_y = LV_CLAMP(PADDLE_H / 2, lpad_y, SCREEN - PADDLE_H / 2);

    /* TODO 2: software-controlled paddle (sau Jucătorul 2 via SW3/SW4)
     * Verificăm dacă sunt folosite SW3 sau SW4 pentru control manual (Mod 2 Jucători).
     * Dacă nu sunt apăsate, AI-ul preia controlul automat.
     */
    if (btns & (1 << HACKPAD_BTN_SW3)) {
        rpad_y -= PADDLE_SPD;
    } else if (btns & (1 << HACKPAD_BTN_SW4)) {
        rpad_y += PADDLE_SPD;
    } else {
        /* Logica AI: urmărește centrul bilei */
        int ball_center_y = ball_y + BALL_SIZE / 2;
        if (rpad_y < ball_center_y) {
            rpad_y += LV_MIN(SW_PLAYER_SPD, ball_center_y - rpad_y);
        } else if (rpad_y > ball_center_y) {
            rpad_y -= LV_MIN(SW_PLAYER_SPD, rpad_y - ball_center_y);
        }
    }
    rpad_y = LV_CLAMP(PADDLE_H / 2, rpad_y, SCREEN - PADDLE_H / 2);

    /* TODO 3: ball movement and wall bounce.
     * Add vx/vy to ball_x/ball_y. If the ball touches the top or bottom, negate vy.
     */
    ball_x += vx;
    ball_y += vy;

    if (ball_y <= 0) {
        ball_y = 0;
        vy = -vy;
    } else if (ball_y >= SCREEN - BALL_SIZE) {
        ball_y = SCREEN - BALL_SIZE;
        vy = -vy;
    }

    /* Oprim flash-urile de LED-uri din frame-ul trecut dacă mingea e în joc */
    hal_leds(0);

    /* TODO 4: paddle collisions. */
    /* Paleta din Stânga (Jucător) */
    if (vx < 0) {
        if (ball_x <= PADDLE_W && ball_x >= 0) {
            if ((ball_y + BALL_SIZE >= lpad_y - PADDLE_H / 2) &&
                (ball_y <= lpad_y + PADDLE_H / 2)) {
                
                // Schimbă direcția pe X și accelerează mingea (Stretch goal)
                vx = -vx;
                if (vx < 7) vx++; // Limită de viteză maximă safe pe X
                
                // Efect/Spin în funcție de distanța față de centrul paletei (Stretch goal)
                int hit_point = (ball_y + BALL_SIZE / 2) - lpad_y;
                vy = hit_point / 5; // Ajustează unghiul de reflexie
            }
        }
    }
    /* Paleta din Dreapta (AI / Jucător 2) */
    else if (vx > 0) {
        if (ball_x + BALL_SIZE >= SCREEN - PADDLE_W && ball_x + BALL_SIZE <= SCREEN) {
            if ((ball_y + BALL_SIZE >= rpad_y - PADDLE_H / 2) &&
                (ball_y <= rpad_y + PADDLE_H / 2)) {
                
                // Schimbă direcția pe X și accelerează mingea (Stretch goal)
                vx = -vx;
                if (vx > -7) vx--;
                
                // Efect/Spin în funcție de distanța față de centrul paletei (Stretch goal)
                int hit_point = (ball_y + BALL_SIZE / 2) - rpad_y;
                vy = hit_point / 5;
            }
        }
    }

    /* TODO 5: scoring. */
    if (ball_x < 0) {
        /* A marcat AI-ul (Dreapta) */
        rscore++;
        lv_label_set_text_fmt(rscore_lbl, "%d", rscore);
        hal_led(HACKPAD_LED_RED, true); // Flash RED LED
        
        if (rscore >= WIN_SCORE) {
            running = false;
            lv_label_set_text(msg_lbl, "SOFTWARE WINS!\nPress SW3 to Restart");
        } else {
            serve(1); // Servește către câștigător
        }
    } 
    else if (ball_x > SCREEN) {
        /* Ai marcat tu (Stânga) */
        lscore++;
        lv_label_set_text_fmt(lscore_lbl, "%d", lscore);
        hal_led(HACKPAD_LED_GREEN, true); // Flash GREEN LED
        
        if (lscore >= WIN_SCORE) {
            running = false;
            lv_label_set_text(msg_lbl, "YOU WIN!\nPress SW3 to Restart");
        } else {
            serve(-1); // Servește către câștigător
        }
    }

    /* Push the state to the screen */
    lv_obj_set_pos(lpad, 0, lpad_y - PADDLE_H / 2);
    lv_obj_set_pos(rpad, SCREEN - PADDLE_W, rpad_y - PADDLE_H / 2);
    lv_obj_set_pos(ball, ball_x, ball_y);
}

int main(void)
{
    hal_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Dashed center line */
    for (int y = 0; y < SCREEN; y += 20) {
        lv_obj_t *dash = make_rect(2, 10, 0x505050);
        lv_obj_set_pos(dash, SCREEN / 2 - 1, y);
    }

    lpad = make_rect(PADDLE_W, PADDLE_H, 0xFFFFFF);
    rpad = make_rect(PADDLE_W, PADDLE_H, 0xFFFFFF);
    ball = make_rect(BALL_SIZE, BALL_SIZE, 0xFFFF00);

    lscore_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(lscore_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lscore_lbl, &lv_font_montserrat_32, 0);
    lv_obj_align(lscore_lbl, LV_ALIGN_TOP_MID, -40, 8);

    rscore_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(rscore_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(rscore_lbl, &lv_font_montserrat_32, 0);
    lv_obj_align(rscore_lbl, LV_ALIGN_TOP_MID, 40, 8);

    msg_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_align(msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg_lbl, LV_ALIGN_CENTER, 0, 40);

    new_game();
    lv_timer_create(game_tick, 16, NULL);

    hal_run();
    return 0;
}
