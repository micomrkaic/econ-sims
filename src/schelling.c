/* schelling.c — Schelling's segregation model, with sliders.
 *
 * Two groups live on a grid with some empty cells. An agent is content if at
 * least a share `tolerance` of its neighbours are of its own group; a
 * discontent agent moves to an empty cell where it would be content. The
 * point of the model is how little preference it takes: agents who would be
 * happy as a 35% minority end up in neighbourhoods that are three-quarters
 * their own kind.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#include "ui.h"
#include "rng.h"

/* ---- the model -------------------------------------------------------- */
#define N 76                      /* the grid is N x N, wrapped at the edges */
#define CELLS (N * N)
enum { EMPTY = 0, GROUP_A = 1, GROUP_B = 2 };

typedef struct {
    unsigned char cell[CELLS];
    int empty[CELLS], n_empty;    /* indices of the empty cells */
    double tolerance;             /* share of like neighbours an agent wants */
    int radius;                   /* neighbourhood: (2r+1)^2 - 1 cells */
    long moves;                   /* moves made since the last reset */
    Rng rng;
} City;

static void city_reset(City *c, double vacancy, double share_a, uint64_t seed)
{
    rng_seed(&c->rng, seed);
    int n_empty = (int)(vacancy * CELLS + 0.5), n_a = (int)(share_a * (CELLS - n_empty) + 0.5);
    for (int i = 0; i < CELLS; i++) c->cell[i] = i < n_empty ? EMPTY : i < n_empty + n_a ? GROUP_A : GROUP_B;
    for (int i = CELLS - 1; i > 0; i--) {             /* Fisher-Yates shuffle */
        int j = rng_below(&c->rng, i + 1);
        unsigned char t = c->cell[i]; c->cell[i] = c->cell[j]; c->cell[j] = t;
    }
    c->n_empty = 0;
    for (int i = 0; i < CELLS; i++) if (c->cell[i] == EMPTY) c->empty[c->n_empty++] = i;
    c->moves = 0;
}

/* The share of `group` among the occupied neighbours of cell i, the cell
 * `skip` not counted (an agent weighing a move must not count itself).
 * Returns -1 if there are no occupied neighbours. */
static double like_share(const City *c, int i, int group, int skip)
{
    int x = i % N, y = i / N, same = 0, occupied = 0;
    for (int dy = -c->radius; dy <= c->radius; dy++)
        for (int dx = -c->radius; dx <= c->radius; dx++) {
            if (!dx && !dy) continue;
            int j = ((y + dy + N) % N) * N + (x + dx + N) % N;
            if (j == skip || c->cell[j] == EMPTY) continue;
            occupied++;
            if (c->cell[j] == group) same++;
        }
    return occupied ? (double)same / occupied : -1.0;
}

static bool content(const City *c, int i, int group, int skip)
{
    double s = like_share(c, i, group, skip);
    return s < 0 || s >= c->tolerance;            /* no neighbours: nothing to object to */
}

/* One attempt: pick an agent at random; if discontent it moves to an empty
 * cell where it would be content (up to 30 tries, else the last one tried). */
static void city_step(City *c)
{
    if (c->n_empty == 0 || c->n_empty == CELLS) return;
    int i;
    do i = rng_below(&c->rng, CELLS); while (c->cell[i] == EMPTY);
    int group = c->cell[i];
    if (content(c, i, group, -1)) return;
    int k = 0;
    for (int t = 0; t < 30; t++) {
        k = rng_below(&c->rng, c->n_empty);
        if (content(c, c->empty[k], group, i)) break;
    }
    int to = c->empty[k];
    c->cell[to] = (unsigned char)group; c->cell[i] = EMPTY; c->empty[k] = i;
    c->moves++;
}

typedef struct { double like, discontent; } CityStats;   /* both shares, 0..1 */

static CityStats city_stats(const City *c)
{
    double sum = 0; int with_neighbours = 0, agents = 0, unhappy = 0;
    for (int i = 0; i < CELLS; i++) {
        if (c->cell[i] == EMPTY) continue;
        agents++;
        double s = like_share(c, i, c->cell[i], -1);
        if (s >= 0) { sum += s; with_neighbours++; if (s < c->tolerance) unhappy++; }
    }
    CityStats st = {with_neighbours ? sum / with_neighbours : 0, agents ? (double)unhappy / agents : 0};
    return st;
}

/* ---- model checks (--test) ------------------------------------------- */
static City city;

static int run_tests(void)
{
    int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { fails++; printf("schelling FAIL: %s\n", what); } } while (0)
    City *c = &city;
    c->radius = 1;

    /* nobody is discontent at zero tolerance, so nobody moves */
    c->tolerance = 0.0; city_reset(c, 0.12, 0.5, 1);
    for (int i = 0; i < 20000; i++) city_step(c);
    CHECK(c->moves == 0 && city_stats(c).discontent == 0, "tolerance 0 leaves everyone in place");

    /* a random city is about half like-neighbours when the groups are equal */
    c->tolerance = 0.35; city_reset(c, 0.12, 0.5, 2);
    CityStats before = city_stats(c);
    CHECK(fabs(before.like - 0.5) < 0.02, "a shuffled city has about 50% like neighbours");

    /* mild preferences, strong segregation: Schelling's result */
    int a0 = 0, b0 = 0, a1 = 0, b1 = 0;
    for (int i = 0; i < CELLS; i++) { a0 += c->cell[i] == GROUP_A; b0 += c->cell[i] == GROUP_B; }
    for (int i = 0; i < 400000; i++) city_step(c);
    CityStats after = city_stats(c);
    for (int i = 0; i < CELLS; i++) { a1 += c->cell[i] == GROUP_A; b1 += c->cell[i] == GROUP_B; }
    CHECK(a0 == a1 && b0 == b1 && c->n_empty == CELLS - a1 - b1, "moves conserve both groups and the vacancies");
    CHECK(after.like > 0.70, "wanting 35% like neighbours yields over 70%");
    CHECK(after.discontent < 0.01, "the city settles: under 1% discontent");
    bool listed = true;
    for (int k = 0; k < c->n_empty; k++) if (c->cell[c->empty[k]] != EMPTY) listed = false;
    CHECK(listed, "the list of empty cells stays true");

    /* more demanding agents segregate more */
    c->tolerance = 0.5; city_reset(c, 0.12, 0.5, 2);
    for (int i = 0; i < 400000; i++) city_step(c);
    CHECK(city_stats(c).like > after.like, "a higher tolerance threshold segregates more");

    /* same seed, same city */
    unsigned long h1 = 0, h2 = 0;
    c->tolerance = 0.35; city_reset(c, 0.12, 0.5, 7);
    for (int i = 0; i < 50000; i++) city_step(c);
    for (int i = 0; i < CELLS; i++) h1 = h1 * 31 + c->cell[i];
    city_reset(c, 0.12, 0.5, 7);
    for (int i = 0; i < 50000; i++) city_step(c);
    for (int i = 0; i < CELLS; i++) h2 = h2 * 31 + c->cell[i];
    CHECK(h1 == h2, "the same seed reproduces the same city");
#undef CHECK
    if (!fails) printf("schelling: 8 model checks passed (35%% wanted -> %.0f%% like neighbours)\n", 100 * after.like);
    return fails;
}

/* ---- the window ------------------------------------------------------- */
static double p_tolerance = 0.35, p_vacancy = 0.12, p_share_a = 0.50, p_radius = 1, p_speed = 40;
static bool show_discontent = true;
static uint64_t seed = 1;
static UiHist h_like, h_discontent;

static void restart(void)
{
    city.tolerance = p_tolerance; city.radius = (int)p_radius;
    city_reset(&city, p_vacancy, p_share_a, seed++);
    h_like.n = h_discontent.n = 0;
}

static void draw(void)
{
    const int gx = 16, gy = 84, px = 8;           /* the grid: 608 x 608 */
    const int sx = UI_PANEL_X, sw = UI_PANEL_W;
    char a[32], b[64];

    ui_header("Schelling's segregation model",
              "Agents move when too few neighbours are like them. Mild wishes, sharp sorting.");

    /* controls: the two that reshape the city restart it; the rest act live */
    int y = 84;
    ui_slider(sx, y, sw, "Like neighbours wanted", &p_tolerance, 0.0, 0.9, 0.01, "%.2f"); y += 46;
    bool again = ui_slider(sx, y, sw, "Empty cells", &p_vacancy, 0.02, 0.5, 0.01, "%.2f"); y += 46;
    again |= ui_slider(sx, y, sw, "Blue share of agents", &p_share_a, 0.1, 0.9, 0.01, "%.2f"); y += 46;
    ui_slider(sx, y, sw, "Neighbourhood radius", &p_radius, 1, 3, 1, "%.0f"); y += 46;
    ui_slider(sx, y, sw, "Attempts per frame", &p_speed, 5, 1000, 5, "%.0f"); y += 50;
    again |= ui_button(sx, y, 150, "Reshuffle") || ui.reset;
    ui_toggle(sx + 170, y + 4, "Mark discontent", &show_discontent); y += 44;
    if (again) restart();
    city.tolerance = p_tolerance; city.radius = (int)p_radius;

    if (!ui.paused) for (int i = 0; i < (int)p_speed; i++) city_step(&city);
    CityStats st = city_stats(&city);
    if (!ui.paused) { ui_hist_push(&h_like, 100 * st.like); ui_hist_push(&h_discontent, 100 * st.discontent); }

    /* the city */
    ui_rect(gx - 1, gy - 1, N * px + 2, N * px + 2, UI_SURFACE);
    for (int i = 0; i < CELLS; i++) {
        if (city.cell[i] == EMPTY) continue;
        int cx = gx + (i % N) * px, cy = gy + (i / N) * px;
        ui_rect(cx, cy, px - 1, px - 1, city.cell[i] == GROUP_A ? UI_S1 : UI_S2);
        if (show_discontent && !content(&city, i, city.cell[i], -1)) ui_rect(cx + 2, cy + 2, 3, 3, UI_INK);
    }
    int kx = gx + N * px + 16, ky = gy;
    ui_key(kx, ky, UI_S1, true, "Blue"); ky += 22;
    ui_key(kx, ky, UI_S2, true, "Orange"); ky += 22;
    ui_key(kx, ky, UI_SURFACE, true, "Empty"); ky += 22;
    if (show_discontent) { ui_rect(kx + 3, ky + 6, 3, 3, UI_INK); ui_text(kx + 16, ky, UI_BODY, UI_INK2, "Discontent"); }
    ui_textf(kx, gy + N * px - 34, UI_SMALL, UI_MUTED, "%ld moves", city.moves);
    ui_text(kx, gy + N * px - 18, UI_SMALL, st.discontent == 0 ? UI_S3 : UI_MUTED, st.discontent == 0 ? "settled" : "sorting...");

    /* what came of it */
    double mixed = p_share_a * p_share_a + (1 - p_share_a) * (1 - p_share_a);
    snprintf(a, sizeof a, "%.0f%%", 100 * st.like);
    snprintf(b, sizeof b, "wanted %.0f%%, mixed %.0f%%", 100 * p_tolerance, 100 * mixed);
    ui_tile(sx, y, sw / 2 - 6, "Like neighbours", a, b);
    snprintf(a, sizeof a, "%.1f%%", 100 * st.discontent);
    ui_tile(sx + sw / 2 + 6, y, sw / 2 - 6, "Discontent agents", a, "want to move");
    y += 76 + 44;

    UiPlot p = ui_plot(sx + 30, y, sw - 30, UI_H - y - 60, "Over time, percent", 0, UI_HIST_CAP - 1, 0, 100, NULL, "%.0f");
    ui_plot_line(&p, NULL, h_like.v, h_like.n, UI_S3);
    ui_plot_line(&p, NULL, h_discontent.v, h_discontent.n, UI_INK2);
    int lx = ui_key(sx + 30, UI_H - 48, UI_S3, false, "Like neighbours");
    ui_key(lx, UI_H - 48, UI_INK2, false, "Discontent");
}

static int app(int argc, char **argv)
{
    if (ui_args(argc, argv, "schelling", "Schelling's segregation model")) return run_tests() ? 1 : 0;
    if (!ui_open("Schelling's segregation model")) return 1;
    restart();
    ui_run(draw);
    return 0;
}
UI_MAIN(app)
