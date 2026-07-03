// SPDX-License-Identifier: GPL-2.0
/*
 * snake - classic Snake (PROJECT IMPLEMENTATION)
 */
#include <stdlib.h>
#include <time.h>
#include "hal.h"

#define CELL      16
#define GRID      15            /* 15 x 16 = 240 px */
#define MAX_LEN   (GRID * GRID)
#define START_LEN 3

enum dir { UP, RIGHT, DOWN, LEFT };

/* Movement deltas indexed by enum dir */
static const int dx[] = { 0, 1, 0, -1 };
static const int dy[] = { -1, 0, 1, 0 };

struct cell {
    int x, y;
};

static struct cell body[MAX_LEN];   
static int length;
static enum dir heading;
static struct cell food;
static bool running;
static int score;

static lv_obj_t *seg_obj[MAX_LEN];  
static lv_obj_t *food_obj;
static lv_obj_t *score_lbl, *msg_lbl;
static lv_timer_t *tick_timer;
static uint32_t tick_period;

static void place_food(void)
{
    bool occupied;
    do {
        occupied = false;
        food.x = rand() % GRID;
        food.y = rand() % GRID;

        for (int i = 0; i < length; i++) {
            if (body[i].x == food.x && body[i].y == food.y) {
                occupied = true;
                break;
            }
        }
    } while (occupied);

    if (food_obj) {
        lv_obj_set_pos(food_obj, food.x * CELL + 2, food.y * CELL + 2);
    }
}

static void update_leds(void)
{
    uint32_t mask = 0;
    if (length >= 5)  mask |= (1 << 0); // RED
    if (length >= 10) mask |= (1 << 1); // GREEN
    if (length >= 15) mask |= (1 << 2); // BLUE
    
    hal_leds(mask);
}

static void redraw(void)
{
    lv_obj_t *scr = lv_screen_active();

    for (int i = 0; i < length; i++) {
        if (seg_obj[i] == NULL) {
            seg_obj[i] = lv_obj_create(scr);
            if (seg_obj[i] != NULL) {
                lv_obj_set_size(seg_obj[i], CELL - 2, CELL - 2);
                lv_obj_set_style_border_width(seg_obj[i], 0, 0);
                lv_obj_set_style_radius(seg_obj[i], 2, 0);
            }
        }

        if (seg_obj[i] != NULL) {
            lv_obj_remove_flag(seg_obj[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(seg_obj[i], body[i].x * CELL + 1, body[i].y * CELL + 1);
            lv_obj_set_style_bg_color(seg_obj[i],
                i == 0 ? lv_color_hex(0x00FF00)   /* cap */
                       : lv_color_hex(0x00A000),  /* corp */
                0);
        }
    }

    for (int i = length; i < MAX_LEN; i++) {
        if (seg_obj[i] != NULL) {
            lv_obj_add_flag(seg_obj[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void new_game(void)
{
    length = START_LEN;
    heading = RIGHT;
    score = 0;
    for (int i = 0; i < length; i++) {
        body[i].x = GRID / 2 - i;
        body[i].y = GRID / 2;
    }
    
    if (score_lbl) lv_label_set_text(score_lbl, "0");
    if (msg_lbl) lv_label_set_text(msg_lbl, "");
    
    tick_period = 220;
    if (tick_timer) lv_timer_set_period(tick_timer, tick_period);
    
    place_food();
    update_leds();
    redraw();
    running = true;
}

void game_over(void)
{
    running = false;
    if (msg_lbl) {
        lv_label_set_text_fmt(msg_lbl, "GAME OVER\nscore %d\nSW3 = restart\nSW2 = exit", score);
    }
}

static void game_tick(lv_timer_t *t)
{
    LV_UNUSED(t);

    /* Logica de restart și ieșire când e Game Over */
    if (!running) {
        if (hal_button_pressed(HACKPAD_BTN_SW3)) {
            new_game();
            return; 
        }
        if (hal_button_pressed(HACKPAD_BTN_SW2)) {
            exit(0); /* Părăsește aplicația */
        }
        return;
    }

    /* Mapare direcții absolute cu prevenirea întoarcerilor la 180 grade */
    if (hal_button_pressed(HACKPAD_BTN_SW3) && heading != DOWN)
        heading = UP;
    else if (hal_button_pressed(HACKPAD_BTN_SW1) && heading != UP)
        heading = DOWN;
    else if (hal_button_pressed(HACKPAD_BTN_SW2) && heading != LEFT)
        heading = RIGHT;
    else if (hal_button_pressed(HACKPAD_BTN_SW4) && heading != RIGHT)
        heading = LEFT;

    struct cell head = {
        body[0].x + dx[heading],
        body[0].y + dy[heading],
    };

    if (head.x < 0 || head.x >= GRID || head.y < 0 || head.y >= GRID) {
        game_over();
        return;
    }
    
    for (int i = 0; i < length; i++) {
        if (head.x == body[i].x && head.y == body[i].y) {
            game_over();
            return;
        }
    }

    bool grow = (head.x == food.x && head.y == food.y);

    if (grow) {
        if (length < MAX_LEN) { 
            for (int i = length; i > 0; i--) {
                body[i] = body[i - 1];
            }
            body[0] = head;
            length++;
            score += 10;
            
            if (score_lbl) lv_label_set_text_fmt(score_lbl, "%d", score);
            place_food();
            update_leds();

            if (tick_period > 80) {
                tick_period -= 10;
                if (tick_timer) lv_timer_set_period(tick_timer, tick_period);
            }
        } else {
            game_over(); 
            return;
        }
    } else {
        for (int i = length - 1; i > 0; i--) {
            body[i] = body[i - 1];
        }
        body[0] = head;
    }

    redraw();
}

int main(void)
{
    srand(time(NULL));
    hal_init();

    lv_obj_t *scr = lv_screen_active();
    if (!scr) return -1; 

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    food_obj = lv_obj_create(scr);
    if (food_obj) {
        lv_obj_set_size(food_obj, CELL - 4, CELL - 4);
        lv_obj_set_style_bg_color(food_obj, lv_color_hex(0xFFFF00), 0);
        lv_obj_set_style_border_width(food_obj, 0, 0);
        lv_obj_set_style_radius(food_obj, LV_RADIUS_CIRCLE, 0);
    }

    score_lbl = lv_label_create(scr);
    if (score_lbl) {
	lv_obj_set_style_text_font(score_lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(score_lbl, lv_color_hex(0x808080), 0);
        lv_obj_align(score_lbl, LV_ALIGN_TOP_RIGHT, -4, 2);
    }

    msg_lbl = lv_label_create(scr);
    if (msg_lbl) {
        lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0xFF4040), 0);
        lv_obj_set_style_text_align(msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(msg_lbl, LV_ALIGN_CENTER, 0, 0);
    }

    tick_timer = lv_timer_create(game_tick, 220, NULL);
    new_game();

    hal_run();
    return 0;
}
