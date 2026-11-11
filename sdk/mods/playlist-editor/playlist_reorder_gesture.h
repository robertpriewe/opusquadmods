/* Main-Browse reorder gesture (R80 lab): one state machine for both inputs.
 *
 *   knob   hold 500 ms on a row (KNOBEV 0x1670658 ev 2) -> grab; turn (KNOBTURN
 *          0x16707f8) moves the held row; press drops; BACK cancels.
 *   touch  finger held still PG_HOLD_MS on a row (RowComp touchDown 0x1e00b70,
 *          timed on the message tick) -> grab; moving the finger (RowComp scroll
 *          slot +0x188, 0x1e02580, swallowed while grabbed) moves the held row by
 *          whole row heights; lifting the finger over the list drops (touchUp
 *          +0x170, 0x1e03500); lifting it off the list, a second finger, or BACK
 *          cancels.
 * Either way the result is one (from, to) pair for plr_plan_move / plr_commit.
 * While grabbed, pg_data_row() is the preview remap the model +0x28 hook
 * (refreshComponentForCell 0x18861d0) uses. Pure: no native calls, no clock;
 * the caller passes times in ms. Message thread only. */
#ifndef OQ_PLAYLIST_REORDER_GESTURE_H
#define OQ_PLAYLIST_REORDER_GESTURE_H

#define PG_HOLD_MS 500 /* stock knob long press; also used for touch */
#define PG_SLOP_PX 12  /* finger travel that turns a press into a stock scroll */

enum pg_state { PG_IDLE, PG_PENDING, PG_GRABBED };
enum pg_source { PG_NONE, PG_KNOB, PG_TOUCH };
enum pg_end { PG_END_NONE, PG_END_COMMIT, PG_END_CANCEL };

struct pg {
    enum pg_state state;
    enum pg_source source;
    int rows;          /* rows in the list when the gesture began */
    int from, to;      /* data row grabbed, display row it would land on */
    long long down_ms; /* touch: when the finger landed */
    int down_y, row_h; /* touch: landing y (list coordinates) and row height */
    enum pg_end end;   /* how the last gesture ended */
    int end_from, end_to;
};

static void pg_reset(struct pg *g) { g->state = PG_IDLE; g->source = PG_NONE; }
static int pg_active(const struct pg *g) { return g->state == PG_GRABBED; }
static int pg_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void pg_finish(struct pg *g, enum pg_end how) {
    g->end = how == PG_END_COMMIT && g->to == g->from ? PG_END_CANCEL : how; /* no-op drop */
    g->end_from = g->from;
    g->end_to = g->to;
    pg_reset(g);
}
/* Takes the result of the last gesture once: 1 with (from, to) to commit, 0 none/cancel. */
static int pg_take_commit(struct pg *g, int *from, int *to) {
    int c = g->end == PG_END_COMMIT;
    if (c) {
        *from = g->end_from;
        *to = g->end_to;
    }
    g->end = PG_END_NONE;
    return c;
}

/* ---- knob ---- */
/* ev 2 on `row`. 1 = ours (swallow stock quick scroll), 0 = leave it to stock. */
static int pg_knob_hold(struct pg *g, int row, int rows, int eligible) {
    if (!eligible || g->state != PG_IDLE || row < 0 || row >= rows || rows < 2)
        return 0;
    *g = (struct pg){.state = PG_GRABBED, .source = PG_KNOB, .rows = rows, .from = row, .to = row,
                     .end = g->end, .end_from = g->end_from, .end_to = g->end_to};
    return 1;
}
/* 1 = swallow the turn (it moved the held row). */
static int pg_knob_turn(struct pg *g, int delta) {
    if (g->state != PG_GRABBED || g->source != PG_KNOB)
        return 0;
    g->to = pg_clamp(g->to + delta, 0, g->rows - 1);
    return 1;
}
static int pg_knob_press(struct pg *g) {
    if (g->state != PG_GRABBED || g->source != PG_KNOB)
        return 0;
    pg_finish(g, PG_END_COMMIT);
    return 1;
}

/* ---- touch ---- */
/* Finger lands on `row`. Never swallowed: until the hold elapses it is stock's. */
static void pg_touch_down(struct pg *g, int row, int rows, int y, int row_h, long long now,
                          int eligible) {
    if (g->state == PG_GRABBED) { /* a second finger while dragging */
        pg_finish(g, PG_END_CANCEL);
        return;
    }
    if (!eligible || row < 0 || row >= rows || rows < 2 || row_h <= 0) {
        pg_reset(g);
        return;
    }
    *g = (struct pg){.state = PG_PENDING, .source = PG_TOUCH, .rows = rows, .from = row, .to = row,
                     .down_ms = now, .down_y = y, .row_h = row_h, .end = g->end,
                     .end_from = g->end_from, .end_to = g->end_to};
}
/* Message tick. 1 when the hold just armed the grab (caller: set RowComp
 * [+0x124] so stock's mouseUp sends no tap, start the preview, show the banner). */
static int pg_tick(struct pg *g, long long now) {
    if (g->state != PG_PENDING || now - g->down_ms < PG_HOLD_MS)
        return 0;
    g->state = PG_GRABBED;
    return 1;
}
/* Finger at y. 1 = swallow (do not scroll the list). */
static int pg_touch_move(struct pg *g, int y) {
    int dy = y - g->down_y;
    if (g->state == PG_PENDING) {
        if (dy > PG_SLOP_PX || dy < -PG_SLOP_PX)
            pg_reset(g); /* it is a scroll: stock's */
        return 0;
    }
    if (g->state != PG_GRABBED || g->source != PG_TOUCH)
        return 0;
    /* whole rows, rounded half away from zero */
    int steps = (dy >= 0 ? dy + g->row_h / 2 : dy - g->row_h / 2) / g->row_h;
    g->to = pg_clamp(g->from + steps, 0, g->rows - 1);
    return 1;
}
/* Finger lifted. over_list = the lift point is inside the list's bounds.
 * 1 = swallow (stock must not treat it as a tap or fling). */
static int pg_touch_up(struct pg *g, int over_list) {
    if (g->state == PG_PENDING) {
        pg_reset(g); /* a short touch: stock's tap */
        return 0;
    }
    if (g->state != PG_GRABBED || g->source != PG_TOUCH)
        return 0;
    pg_finish(g, over_list ? PG_END_COMMIT : PG_END_CANCEL);
    return 1;
}

/* ---- shared ---- */
/* BACK, the list changing or leaving the screen: 1 when a grab was cancelled. */
static int pg_cancel(struct pg *g) {
    int was = g->state == PG_GRABBED;
    if (was)
        pg_finish(g, PG_END_CANCEL);
    else
        pg_reset(g);
    return was;
}
/* Preview: the data row to draw at display row r. */
static int pg_data_row(const struct pg *g, int r) {
    if (g->state != PG_GRABBED || g->to == g->from)
        return r;
    if (r == g->to)
        return g->from;
    if (g->from < g->to && r >= g->from && r < g->to)
        return r + 1;
    if (g->from > g->to && r > g->to && r <= g->from)
        return r - 1;
    return r;
}
#endif
