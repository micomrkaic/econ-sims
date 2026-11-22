/* orderbook.c — a limit order book with random and informed traders.
 *
 * The market of the "zero-intelligence" literature (Daniels, Farmer and
 * others, 2003). Traders post limit orders — offers to buy below the best
 * ask, offers to sell above the best bid — at prices chosen at random
 * within a window; each resting order is cancelled at a constant rate; and
 * market orders arrive to trade at once against the best price on the other
 * side. Nobody optimises anything, yet the book has a spread, a depth
 * profile and price impact, all set by the three order-flow rates.
 *
 * One addition: the asset has a fundamental value that wanders, and a share
 * of market orders comes from traders who know it. They buy when the ask is
 * below the value and sell when the bid is above it. That is enough for the
 * price to track the value; without them it drifts free.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#include "ui.h"
#include "rng.h"

/* ---- the model -------------------------------------------------------- */
#define LEVELS 400                /* price levels held in memory; the book is re-centred as the price moves */
#define WINDOW 20                 /* limit orders land within this many ticks of the opposite best price */
#define TICK 0.1                  /* price per tick */
#define BASE_PRICE 100.0

typedef struct {
    /* order flow, per unit of time and per side */
    double limit_rate;            /* limit orders posted */
    double market_rate;           /* market orders sent */
    double cancel_rate;           /* cancellations, per resting order */
    double informed;              /* share of market orders from traders who know the value */
    double value_vol;             /* how fast the fundamental value wanders, ticks per sqrt(time) */
    /* the book */
    int bid[LEVELS], ask[LEVELS]; /* resting quantity at each level */
    int best_bid, best_ask;       /* level of the best prices; -1 / LEVELS if that side is empty */
    long depth_bid, depth_ask;    /* total resting quantity per side */
    long shift;                   /* levels the book has been re-centred by, so prices stay continuous */
    double value;                 /* fundamental value, in levels (same frame as the book) */
    double last_mid;              /* the last mid seen while both sides had orders */
    double time;
    long placed, cancelled, traded, unfilled;
    Rng rng;
} Book;

static double book_mid(const Book *b)
{
    if (b->best_bid >= 0 && b->best_ask < LEVELS) return 0.5 * (b->best_bid + b->best_ask);
    return b->last_mid;
}
static double level_price(const Book *b, double level) { return BASE_PRICE + (level + b->shift - LEVELS / 2) * TICK; }

static void fix_best(Book *b)
{
    while (b->best_bid >= 0 && b->bid[b->best_bid] == 0) b->best_bid--;
    while (b->best_ask < LEVELS && b->ask[b->best_ask] == 0) b->best_ask++;
    if (b->best_bid >= 0 && b->best_ask < LEVELS) b->last_mid = 0.5 * (b->best_bid + b->best_ask);
}

/* Keep the action in the middle of the arrays: slide everything by `by`
 * levels, dropping whatever falls off the far end. */
static void recentre(Book *b)
{
    int by = (int)floor(book_mid(b)) - LEVELS / 2;
    if (abs(by) < LEVELS / 4) return;
    static int nb[LEVELS], na[LEVELS];
    b->depth_bid = b->depth_ask = 0;
    for (int i = 0; i < LEVELS; i++) {
        int j = i + by;
        nb[i] = j >= 0 && j < LEVELS ? b->bid[j] : 0;
        na[i] = j >= 0 && j < LEVELS ? b->ask[j] : 0;
        b->depth_bid += nb[i]; b->depth_ask += na[i];
    }
    memcpy(b->bid, nb, sizeof nb); memcpy(b->ask, na, sizeof na);
    b->shift += by; b->value -= by; b->last_mid -= by;
    b->best_bid = LEVELS - 1; b->best_ask = 0;
    while (b->best_bid >= 0 && b->bid[b->best_bid] == 0) b->best_bid--;
    while (b->best_ask < LEVELS && b->ask[b->best_ask] == 0) b->best_ask++;
}

static void book_open(Book *b, uint64_t seed)
{
    rng_seed(&b->rng, seed);
    memset(b->bid, 0, sizeof b->bid); memset(b->ask, 0, sizeof b->ask);
    b->best_bid = -1; b->best_ask = LEVELS; b->depth_bid = b->depth_ask = 0; b->shift = 0;
    b->value = b->last_mid = LEVELS / 2; b->time = 0;
    b->placed = b->cancelled = b->traded = b->unfilled = 0;
}

static void limit_order(Book *b, bool buy)
{
    double mid = book_mid(b);
    if (buy) {
        int top = b->best_ask < LEVELS ? b->best_ask - 1 : (int)floor(mid);     /* never at or above the best ask */
        int lvl = top - rng_below(&b->rng, WINDOW);
        if (lvl < 0) return;
        b->bid[lvl]++; b->depth_bid++; if (lvl > b->best_bid) b->best_bid = lvl;
    } else {
        int bottom = b->best_bid >= 0 ? b->best_bid + 1 : (int)ceil(mid);
        int lvl = bottom + rng_below(&b->rng, WINDOW);
        if (lvl >= LEVELS) return;
        b->ask[lvl]++; b->depth_ask++; if (lvl < b->best_ask) b->best_ask = lvl;
    }
    b->placed++;
    fix_best(b);
}

/* A market order for `size` units: it takes the best prices on the other
 * side, walking through the book for as long as it must. Returns the
 * quantity filled. */
static int market_order(Book *b, bool buy, int size)
{
    int filled = 0;
    while (filled < size) {
        if (buy) {
            if (b->best_ask >= LEVELS) break;
            b->ask[b->best_ask]--; b->depth_ask--;
        } else {
            if (b->best_bid < 0) break;
            b->bid[b->best_bid]--; b->depth_bid--;
        }
        filled++;
        fix_best(b);
    }
    b->traded += filled; b->unfilled += size - filled;
    return filled;
}

static void cancel_one(Book *b)
{
    long total = b->depth_bid + b->depth_ask;
    if (total == 0) return;
    long k = (long)(rng_uniform(&b->rng) * total);
    if (k < b->depth_bid) {
        for (int i = b->best_bid; i >= 0; i--) { if (k < b->bid[i]) { b->bid[i]--; b->depth_bid--; break; } k -= b->bid[i]; }
    } else {
        k -= b->depth_bid;
        for (int i = b->best_ask; i < LEVELS; i++) { if (k < b->ask[i]) { b->ask[i]--; b->depth_ask--; break; } k -= b->ask[i]; }
    }
    b->cancelled++;
    fix_best(b);
}

/* One event, drawn in proportion to the rates (Gillespie's method); time
 * moves on by the expected wait. */
static void book_event(Book *b)
{
    double r_limit = 2 * b->limit_rate, r_market = 2 * b->market_rate;
    double r_cancel = b->cancel_rate * (double)(b->depth_bid + b->depth_ask), total = r_limit + r_market + r_cancel;
    if (total <= 0) return;
    double dt = 1.0 / total, u = rng_uniform(&b->rng) * total;
    b->time += dt;
    b->value += b->value_vol * sqrt(dt) * rng_normal(&b->rng);
    if (u < r_limit) limit_order(b, u < b->limit_rate);
    else if (u < r_limit + r_market) {
        if (rng_uniform(&b->rng) < b->informed) {
            /* an informed trader acts only on a mispricing it can see */
            if (b->best_ask < LEVELS && b->value > b->best_ask) market_order(b, true, 1);
            else if (b->best_bid >= 0 && b->value < b->best_bid) market_order(b, false, 1);
        } else market_order(b, rng_uniform(&b->rng) < 0.5, 1);
    } else cancel_one(b);
    if (b->best_bid >= 0 && b->best_ask < LEVELS) recentre(b);
}

static void book_run(Book *b, double duration)
{
    double until = b->time + duration;
    for (int guard = 0; b->time < until && guard < 200000; guard++) book_event(b);
}

static double book_spread(const Book *b)      /* in ticks; NAN if a side is empty */
{
    return b->best_bid >= 0 && b->best_ask < LEVELS ? (double)(b->best_ask - b->best_bid) : NAN;
}

/* ---- model checks (--test) ------------------------------------------- */
static Book book;

static void set_defaults(Book *b)
{
    b->limit_rate = 60; b->market_rate = 12; b->cancel_rate = 0.12; b->informed = 0.3; b->value_vol = 0.6;
}
/* long-run averages: spread (ticks) and how far the mid sits from the value (ticks) */
static void averages(Book *b, uint64_t seed, double *spread, double *error)
{
    double s = 0, e = 0; int n = 0;
    book_open(b, seed);
    book_run(b, 50);                              /* let the book fill */
    for (int k = 0; k < 4000; k++) {
        book_run(b, 0.25);
        double sp = book_spread(b);
        if (!isnan(sp)) { s += sp; e += fabs(book_mid(b) - b->value); n++; }
    }
    *spread = n ? s / n : NAN; *error = n ? e / n : NAN;
}

static int run_tests(void)
{
    int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { fails++; printf("orderbook FAIL: %s\n", what); } } while (0)
    Book *b = &book;
    set_defaults(b); book_open(b, 1);

    /* the book never crosses, never goes negative, and its totals stay true */
    bool sound = true;
    for (int k = 0; k < 300000; k++) {
        book_event(b);
        if (b->best_bid >= 0 && b->best_ask < LEVELS && b->best_bid >= b->best_ask) sound = false;
        if (b->best_bid >= 0 && b->bid[b->best_bid] <= 0) sound = false;
        if (b->best_ask < LEVELS && b->ask[b->best_ask] <= 0) sound = false;
    }
    long sb = 0, sa = 0; bool nonneg = true, sides = true;
    for (int i = 0; i < LEVELS; i++) {
        sb += b->bid[i]; sa += b->ask[i];
        if (b->bid[i] < 0 || b->ask[i] < 0) nonneg = false;
        if ((b->bid[i] && i > b->best_bid) || (b->ask[i] && i < b->best_ask)) sides = false;
    }
    CHECK(sound, "the best bid stays below the best ask, each with orders behind it");
    CHECK(nonneg && sides, "no negative quantities, no bids above the best bid, no asks below the best ask");
    CHECK(sb == b->depth_bid && sa == b->depth_ask, "the depth totals match the book");
    CHECK(b->placed > 0 && b->traded > 0 && b->cancelled > 0, "orders are placed, traded and cancelled");

    /* the rates set the spread: the zero-intelligence comparative statics */
    double s0, e0, s_thin, s_busy, e, e_none;
    averages(b, 3, &s0, &e0);
    b->limit_rate = 25; averages(b, 3, &s_thin, &e);
    set_defaults(b); b->market_rate = 30; averages(b, 3, &s_busy, &e);
    CHECK(s0 > 1.0 && s0 < 15.0, "a spread of a few ticks emerges on its own");
    CHECK(s_thin > s0 * 1.2, "fewer limit orders, wider spread");
    CHECK(s_busy > s0 * 1.2, "more market orders, wider spread");

    /* informed traders tie the price to the value */
    set_defaults(b); b->informed = 0.0; averages(b, 3, &s_thin, &e_none);
    CHECK(e0 < 0.5 * e_none, "with informed traders the price tracks the value far more closely");
    CHECK(e0 < 6.0, "the price stays within a few ticks of the value");

    /* a large order moves the price; the book then recovers */
    set_defaults(b); b->value_vol = 0; book_open(b, 5); book_run(b, 60);
    double before = book_mid(b);
    int filled = market_order(b, false, 150);
    double after = book_mid(b);
    book_run(b, 40);
    double later = book_mid(b);
    CHECK(filled > 0 && after < before - 3, "a large sell order knocks the price down through the book");
    CHECK(fabs(later - b->value) < fabs(after - b->value), "and the price comes back towards the value");

    /* an order with nothing to hit is simply unfilled */
    book_open(b, 6);
    CHECK(market_order(b, true, 5) == 0 && b->unfilled == 5, "a market order against an empty book fills nothing");

    /* a long drift re-centres the book without breaking prices */
    set_defaults(b); b->value_vol = 6; book_open(b, 7);
    long shift_seen = 0; double jump = 0;
    for (int k = 0; k < 600; k++) {
        double p0 = level_price(b, book_mid(b));
        book_run(b, 1.0);
        if (b->shift != shift_seen) { double d = fabs(level_price(b, book_mid(b)) - p0); if (d > jump) jump = d; shift_seen = b->shift; }
    }
    CHECK(b->shift != 0 && b->best_bid < b->best_ask && fabs(book_mid(b) - LEVELS / 2.0) < LEVELS / 4.0 + 5,
          "the book is re-centred as the price wanders off");
    CHECK(jump < 1.0, "and re-centring leaves the price where it was");
#undef CHECK
    if (!fails) printf("orderbook: 15 model checks passed (spread %.1f ticks; price error %.1f ticks with informed traders, %.1f without)\n", s0, e0, e_none);
    return fails;
}

/* ---- the window ------------------------------------------------------- */
static double p_limit = 60, p_market = 12, p_cancel = 0.12, p_informed = 0.30, p_vol = 0.6, p_speed = 0.25;
static UiHist h_mid, h_value;
static double avg_spread, avg_error, avg_depth;
static uint64_t seed = 1;

static void settings(void)
{
    book.limit_rate = p_limit; book.market_rate = p_market; book.cancel_rate = p_cancel;
    book.informed = p_informed; book.value_vol = p_vol;
}
static void restart(void)
{
    settings();
    book_open(&book, seed++);
    book_run(&book, 40);                          /* open with a book already built */
    h_mid.n = h_value.n = 0; avg_spread = avg_error = avg_depth = 0;
}

static void draw(void)
{
    const int sx = UI_PANEL_X, sw = UI_PANEL_W;
    char a[32];
    Book *b = &book;
    ui_header("A limit order book", "Random orders make a market. A few informed traders make the price mean something.");

    int y = 84;
    ui_slider(sx, y, sw, "Limit orders per second, a side", &p_limit, 5, 200, 1, "%.0f"); y += 46;
    ui_slider(sx, y, sw, "Market orders per second, a side", &p_market, 1, 60, 1, "%.0f"); y += 46;
    ui_slider(sx, y, sw, "Cancellations per order, a second", &p_cancel, 0.02, 1.0, 0.01, "%.2f"); y += 46;
    ui_slider(sx, y, sw, "Informed share of market orders", &p_informed, 0.0, 1.0, 0.05, "%.2f"); y += 46;
    ui_slider(sx, y, sw, "Volatility of the value", &p_vol, 0.0, 3.0, 0.1, "%.1f"); y += 46;
    ui_slider(sx, y, sw, "Seconds per frame", &p_speed, 0.05, 1.0, 0.05, "%.2f"); y += 50;
    settings();
    if (ui_button(sx, y, 118, "Big sell")) market_order(b, false, 150);
    if (ui_button(sx + 127, y, 118, "Big buy")) market_order(b, true, 150);
    if (ui_button(sx + 254, y, 118, "Restart") || ui.reset) restart();
    y += 42;

    if (!ui.paused) {
        book_run(b, p_speed);
        double sp = book_spread(b), mid = book_mid(b);
        ui_hist_push(&h_mid, level_price(b, mid));
        ui_hist_push(&h_value, level_price(b, b->value));
        if (!isnan(sp)) {
            double near = 0;
            for (int k = 0; k < 5; k++) {
                if (b->best_bid - k >= 0) near += b->bid[b->best_bid - k];
                if (b->best_ask + k < LEVELS) near += b->ask[b->best_ask + k];
            }
            double w = avg_spread == 0 ? 1.0 : 0.02;          /* a slow moving average, for steady tiles */
            avg_spread += w * (sp * TICK - avg_spread);
            avg_error += w * (fabs(mid - b->value) * TICK - avg_error);
            avg_depth += w * (near / 2 - avg_depth);
        }
    }

    /* tiles */
    int tw = sw / 3 - 8;
    snprintf(a, sizeof a, "%.2f", avg_spread);
    ui_tile(sx, y, tw, "Spread", a, "ask - bid");
    snprintf(a, sizeof a, "%.2f", avg_error);
    ui_tile(sx + tw + 12, y, tw, "Mispricing", a, "|price-value|");
    snprintf(a, sizeof a, "%.0f", avg_depth);
    ui_tile(sx + 2 * (tw + 12), y, tw, "Depth", a, "near the mid");
    y += 92;
    ui_textf(sx, y, UI_SMALL, UI_MUTED, "%ld traded, %ld cancelled, %ld resting", b->traded, b->cancelled, b->depth_bid + b->depth_ask);
    y += 14;
    ui_text(sx, y, UI_SMALL, UI_MUTED, "Averages are over the last few seconds.");

    /* the book: quantity resting at each price, bids left, asks right */
    const int dx = 64, dy = 112, dw = 700, dh = 240, span = 50;
    int centre = (int)floor(book_mid(b));
    double peak = 10;
    for (int i = centre - span; i <= centre + span; i++)
        if (i >= 0 && i < LEVELS) { if (b->bid[i] > peak) peak = b->bid[i]; if (b->ask[i] > peak) peak = b->ask[i]; }
    static double ytop = 40;
    ytop += ((peak * 1.15 > 40 ? peak * 1.15 : 40) - ytop) * 0.05;
    UiPlot dp = ui_plot(dx, dy, dw, dh, "Orders resting at each price", level_price(b, centre - span), level_price(b, centre + span + 1),
                        0, ytop, "%.1f", "%.0f");
    int bw = dw / (2 * span + 1);
    for (int i = centre - span; i <= centre + span; i++) {
        if (i < 0 || i >= LEVELS) continue;
        int x = ui_px(&dp, level_price(b, i));
        if (b->bid[i]) { int yt = ui_py(&dp, b->bid[i] > ytop ? ytop : b->bid[i]); ui_rect(x + 1, yt, bw - 1, dy + dh - yt, UI_S1); }
        if (b->ask[i]) { int yt = ui_py(&dp, b->ask[i] > ytop ? ytop : b->ask[i]); ui_rect(x + 1, yt, bw - 1, dy + dh - yt, UI_S2); }
    }
    int vx = ui_px(&dp, level_price(b, b->value + 0.5));
    if (vx > dx && vx < dx + dw) { ui_line(vx, dy, vx, dy + dh, UI_S3); ui_text(vx + 5, dy + 4, UI_SMALL, UI_INK2, "value"); }
    int kx = ui_key(dx, dy + dh + 22, UI_S1, true, "Bids (to buy)");
    kx = ui_key(kx, dy + dh + 22, UI_S2, true, "Asks (to sell)");
    ui_key(kx, dy + dh + 22, UI_S3, false, "Fundamental value");

    /* price against value, over time */
    const int px0 = 64, py0 = 442, pw = 700, ph = 200;
    double lo = 1e18, hi = -1e18;
    for (int i = 0; i < h_mid.n; i++) {
        double lowest = h_mid.v[i] < h_value.v[i] ? h_mid.v[i] : h_value.v[i];
        double highest = h_mid.v[i] > h_value.v[i] ? h_mid.v[i] : h_value.v[i];
        if (lowest < lo) lo = lowest;
        if (highest > hi) hi = highest;
    }
    if (h_mid.n == 0) { lo = BASE_PRICE - 1; hi = BASE_PRICE + 1; }
    if (hi - lo < 2) { double m = 0.5 * (hi + lo); lo = m - 1; hi = m + 1; }
    static double ylo = BASE_PRICE - 1, yhi = BASE_PRICE + 1;
    ylo += (lo - 0.15 * (hi - lo) - ylo) * 0.1; yhi += (hi + 0.15 * (hi - lo) - yhi) * 0.1;
    UiPlot pp = ui_plot(px0, py0, pw, ph, "Price and value", 0, UI_HIST_CAP - 1, ylo, yhi, NULL, "%.1f");
    ui_plot_line(&pp, NULL, h_value.v, h_value.n, UI_S3);
    ui_plot_line(&pp, NULL, h_mid.v, h_mid.n, UI_INK);
    kx = ui_key(px0, py0 + ph + 12, UI_INK, false, "Mid price");
    ui_key(kx, py0 + ph + 12, UI_S3, false, "Fundamental value");
}

static int app(int argc, char **argv)
{
    if (ui_args(argc, argv, "orderbook", "a limit order book with random and informed traders")) return run_tests() ? 1 : 0;
    if (!ui_open("A limit order book")) return 1;
    restart();
    ui_run(draw);
    return 0;
}
UI_MAIN(app)
