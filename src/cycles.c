/* cycles.c — business cycles from technology shocks, running live.
 *
 * The real business cycle model: households choose consumption, saving and
 * hours of work; firms rent capital and labour; the only disturbance is to
 * technology. The model below is text. Cozy's dsge package, running inside
 * this program, checks its steady state, takes exact derivatives there and
 * solves for the decision rule, every time a slider moves. C steps the economy a
 * quarter at a time and plots it, in percent from the steady state, and
 * keeps the business-cycle table: how volatile each series is relative to
 * output and how closely it moves with output, as the model implies and as
 * this run has turned out.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#include "ui.h"
#include "rng.h"
#include "models.h"

/* ---- the model: its text is in models.h ------------------------------ */
enum { Y = RBC_Y, C = RBC_C, I = RBC_I, N = RBC_N, K = RBC_K, A = RBC_A, NV = RBC_NV };
static const char *const NAMES[NV] = {"Output", "Consumption", "Investment", "Hours", "Capital", "Technology"};
#define ALPHA RBC_ALPHA
#define BETA RBC_BETA

typedef struct { double rho, sd, sigma, phi, delta; } Economy;   /* sd and delta in percent */
static const Economy TEXTBOOK = {0.95, 0.7, 1.0, 1.0, 2.5};

typedef struct {
    double P[NV * NV], Q[NV];                   /* y = P y(-1) + Q e, in percent from steady state */
    double ss[NV];                              /* the steady state, in levels */
    double sd[NV], corr[NV];                    /* theory: standard deviations, correlations with output */
} Solution;

/* Ask Cozy for the steady state, the decision rule in percent deviations
 * (RBC_SOLVE, in models.h) and the moments it implies. */
static bool model_solve(const Economy *e, Solution *s)
{
    if (!macro_run(RBC_SOLVE "let q_ = dsge.moments(t_); let sd_ = q_.sd; let co_ = q_.corr[:, 1];",
                   e->rho, e->sigma, e->phi, e->delta / 100, e->sd)) return false;
    return macro_read("P_", s->P, NV, NV) && macro_read("Q_", s->Q, NV, 1) && macro_read("ss_", s->ss, NV, 1)
        && macro_read("sd_", s->sd, NV, 1) && macro_read("co_", s->corr, NV, 1);
}

/* One quarter: y = P y + Q e. */
static void economy_step(const Solution *s, double *y, double e)
{
    double next[NV];
    for (int i = 0; i < NV; i++) {
        next[i] = s->Q[i] * e;
        for (int j = 0; j < NV; j++) next[i] += s->P[i * NV + j] * y[j];
    }
    memcpy(y, next, sizeof next);
}

/* ---- model checks (--test) ------------------------------------------- */
static int run_tests(void)
{
    int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { fails++; printf("cycles FAIL: %s\n", what); } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-8)
    Solution s;
    Economy e = TEXTBOOK;
    CHECK(macro_start() && rbc_define(), "Cozy starts, loads the dsge package and compiles the model text");
    CHECK(model_solve(&e, &s), "Cozy accepts the steady state and solves the model");
    double ms = macro_ms;

    /* the steady state has the great ratios the algebra gives */
    double d = e.delta / 100, ky = ALPHA / (1 / BETA - 1 + d);
    CHECK(NEAR(s.ss[K] / s.ss[Y], ky) && NEAR(s.ss[I] / s.ss[Y], d * ky), "the steady state has the capital and investment ratios the algebra gives");
    CHECK(NEAR(s.ss[Y], s.ss[C] + s.ss[I]) && NEAR(s.ss[A], 0), "in the steady state output is consumed or invested");

    /* the decision rule obeys the model's own identities, in percent */
    double sc = s.ss[C] / s.ss[Y], si = s.ss[I] / s.ss[Y];
    bool resource = NEAR(s.Q[Y], sc * s.Q[C] + si * s.Q[I]);
    for (int j = 0; j < NV; j++) resource = resource && NEAR(s.P[Y * NV + j], sc * s.P[C * NV + j] + si * s.P[I * NV + j]);
    CHECK(resource, "the rule satisfies the resource constraint");
    CHECK(NEAR(s.P[Y * NV + K], ALPHA + (1 - ALPHA) * s.P[N * NV + K]) && NEAR(s.Q[Y], 1 + (1 - ALPHA) * s.Q[N]), "the rule satisfies the production function");
    CHECK(NEAR(s.P[K * NV + K], 1 - d + d * s.P[I * NV + K]) && NEAR(s.P[A * NV + A], e.rho), "the rule satisfies capital accumulation, and technology keeps its persistence");
    bool states = true;
    for (int i = 0; i < NV; i++) for (int j = 0; j < NV; j++) if (j != K && j != A && fabs(s.P[i * NV + j]) > 1e-12) states = false;
    CHECK(states, "only capital and technology carry the economy's history");

    /* the business-cycle facts the model is known for */
    CHECK(s.sd[C] < s.sd[Y] && s.sd[I] > 2 * s.sd[Y] && s.sd[N] < s.sd[Y], "consumption is smoother than output, investment over twice as volatile");
    CHECK(s.corr[C] > 0.5 && s.corr[I] > 0.5 && s.corr[N] > 0.5 && NEAR(s.corr[Y], 1), "consumption, investment and hours all move with output");
    CHECK(s.Q[Y] > 1 && s.Q[N] > 0, "a technology shock raises hours, so output rises by more than technology");

    /* the economy stepped in C has the moments Cozy computed */
    Rng rng; rng_seed(&rng, 1);
    double y[NV] = {0}, s2[NV] = {0}, sy[NV] = {0};
    const int T = 1000000;
    for (int t = 0; t < T; t++) {
        economy_step(&s, y, e.sd * rng_normal(&rng));
        for (int i = 0; i < NV; i++) { s2[i] += y[i] * y[i]; sy[i] += y[i] * y[Y]; }
    }
    bool moments = true;
    for (int i = 0; i < NV; i++) {
        if (fabs(sqrt(s2[i] / T) / s.sd[i] - 1) > 0.03) moments = false;
        if (fabs(sy[i] / sqrt(s2[i] * s2[Y]) - s.corr[i]) > 0.02) moments = false;
    }
    CHECK(moments, "simulated volatilities and correlations match Cozy's theoretical ones");

    /* comparative statics */
    Solution a, b;
    Economy stiff = TEXTBOOK; stiff.phi = 6;
    CHECK(model_solve(&stiff, &a) && a.sd[N] / a.sd[Y] < s.sd[N] / s.sd[Y], "a less elastic labour supply makes hours less volatile");
    Economy brief = TEXTBOOK; brief.rho = 0.5;
    CHECK(model_solve(&brief, &b) && b.sd[Y] < s.sd[Y], "less persistent technology makes output less volatile");

    /* every corner of the sliders' ranges solves */
    bool corners = true;
    const double R[2] = {0.0, 0.99}, S[2] = {0.5, 6}, F[2] = {0, 10}, D[2] = {1, 10};
    for (int m = 0; m < 16; m++) {
        Economy c = {R[m & 1], 0.7, S[m >> 1 & 1], F[m >> 2 & 1], D[m >> 3 & 1]};
        if (!model_solve(&c, &a)) { corners = false; printf("  corner %d: %s\n", m, macro_error); }
    }
    CHECK(corners, "the model solves at every corner of the sliders' ranges");
#undef CHECK
#undef NEAR
    if (!fails) printf("cycles: 14 model checks passed (Cozy %s solves the model in %.2f ms)\n", cozy_version(), ms);
    return fails;
}

/* ---- the window ------------------------------------------------------- */
#define NQ 200                                   /* quarters on screen */
static Economy econ;
static Solution sol;
static double y[NV], pending;
static double hist[NV][NQ];
static int filled;
static double run2[NV], runy[NV];                 /* this run: sums of squares, and of products with output */
static long run_n;
static double p_speed = 12, clock_q, solve_ms;
static Rng rng;
static bool broken;

static void clear_run(void) { memset(run2, 0, sizeof run2); memset(runy, 0, sizeof runy); run_n = 0; }
static void resolve(void)
{
    broken = !model_solve(&econ, &sol);
    solve_ms = macro_ms;
    clear_run();
}
static void clear_history(void)
{
    memset(y, 0, sizeof y); memset(hist, 0, sizeof hist);
    filled = 0; pending = 0;
    clear_run();
}

static double peak(const int *series, int n, double floor)
{
    double m = floor;
    for (int k = 0; k < n; k++) {
        int v = series[k];
        if (!broken && 3.0 * sol.sd[v] > m) m = 3.0 * sol.sd[v];
        for (int i = NQ - filled; i < NQ; i++) if (fabs(hist[v][i]) * 1.1 > m) m = fabs(hist[v][i]) * 1.1;
    }
    return m;
}

static void draw(void)
{
    const int sx = UI_PANEL_X, sw = UI_PANEL_W;
    char a[32], b[64];
    ui_header("Business cycles, live", "Technology shocks, and households that respond to them. Cozy solves the model; the economy runs on the answer.");

    int yy = 84;
    bool moved = false;
    moved |= ui_slider(sx, yy, sw, "Persistence of technology", &econ.rho, 0.0, 0.99, 0.01, "%.2f"); yy += 46;
    moved |= ui_slider(sx, yy, sw, "Size of technology shocks, %", &econ.sd, 0.1, 2.0, 0.05, "%.2f"); yy += 46;
    moved |= ui_slider(sx, yy, sw, "Risk aversion", &econ.sigma, 0.5, 6.0, 0.1, "%.1f"); yy += 46;
    moved |= ui_slider(sx, yy, sw, "Reluctance to vary hours", &econ.phi, 0.0, 10.0, 0.25, "%.2f"); yy += 46;
    moved |= ui_slider(sx, yy, sw, "Depreciation, % a quarter", &econ.delta, 1.0, 10.0, 0.25, "%.2f"); yy += 46;
    ui_slider(sx, yy, sw, "Quarters per second", &p_speed, 1, 30, 1, "%.0f"); yy += 50;
    if (moved) resolve();
    if (ui_button(sx, yy, 118, "Boom")) pending += 3 * econ.sd;
    if (ui_button(sx + 127, yy, 118, "Bust")) pending -= 3 * econ.sd;
    if (ui_button(sx + 254, yy, 118, "Clear") || ui.reset) clear_history();
    yy += 44;

    if (!ui.paused && !broken) {
        for (clock_q += p_speed / 60.0; clock_q >= 1.0; clock_q -= 1.0) {
            economy_step(&sol, y, econ.sd * rng_normal(&rng) + pending);
            pending = 0;
            for (int k = 0; k < NV; k++) {
                memmove(hist[k], hist[k] + 1, (NQ - 1) * sizeof(double)); hist[k][NQ - 1] = y[k];
                run2[k] += y[k] * y[k]; runy[k] += y[k] * y[Y];
            }
            run_n++;
            if (filled < NQ) filled++;
        }
    }

    /* the business-cycle table: what the model implies, and this run so far */
    ui_text(sx, yy, UI_BODY, UI_INK, "The business-cycle table"); yy += 24;
    ui_text(sx, yy, UI_SMALL, UI_MUTED, "              volatility,       correlation"); yy += 11;
    ui_text(sx, yy, UI_SMALL, UI_MUTED, "              output = 1        with output"); yy += 15;
    ui_text(sx, yy, UI_SMALL, UI_MUTED, "              model    run      model    run"); yy += 14;
    static const int ROWS[] = {C, I, N, K};
    for (int r = 0; r < 4; r++, yy += 20) {
        int v = ROWS[r];
        ui_text(sx, yy, UI_BODY, UI_INK2, NAMES[v]);
        if (broken) continue;
        ui_textf(sx + 14 * 8, yy, UI_BODY, UI_INK, "%5.2f", sol.sd[v] / sol.sd[Y]);
        ui_textf(sx + 32 * 8, yy, UI_BODY, UI_INK, "%5.2f", sol.corr[v]);
        if (run_n > 8 && run2[Y] > 0 && run2[v] > 0) {
            ui_textf(sx + 22 * 8, yy, UI_BODY, UI_MUTED, "%5.2f", sqrt(run2[v] / run2[Y]));
            ui_textf(sx + 40 * 8, yy, UI_BODY, UI_MUTED, "%5.2f", runy[v] / sqrt(run2[v] * run2[Y]));
        }
    }
    yy += 8;
    int tw = sw / 2 - 6;
    if (!broken) snprintf(a, sizeof a, "%.2f%%", sol.sd[Y]); else snprintf(a, sizeof a, "-");
    if (run_n > 8) snprintf(b, sizeof b, "this run: %.2f%%", sqrt(run2[Y] / run_n)); else snprintf(b, sizeof b, "this run: -");
    ui_tile(sx, yy, tw, "Output volatility", a, b);
    if (!broken) snprintf(a, sizeof a, "%.1f%%", 100 * sol.ss[I] / sol.ss[Y]); else snprintf(a, sizeof a, "-");
    ui_tile(sx + tw + 12, yy, tw, "Investment share", a, "of output");
    yy += 86;
    ui_textf(sx, yy, UI_SMALL, UI_MUTED, "This run: %ld quarters since the last change.", run_n);

    /* output, consumption and investment on one scale: the classic picture */
    static double xs[NQ];
    for (int i = 0; i < NQ; i++) xs[i] = i - (NQ - 1);
    static double r1 = 10, r2 = 4;
    static const int TOP[] = {Y, C, I}, LOW[] = {N, A};
    r1 += (peak(TOP, 3, 2) - r1) * 0.05;
    r2 += (peak(LOW, 2, 1) - r2) * 0.05;
    const int cx = 64, cw = 700;
    UiPlot p = ui_plot(cx, 112, cw, 180, NULL, -(NQ - 1), 0, -r1, r1, NULL, "%.0f");
    ui_text(cx, 86, UI_BODY, UI_INK, "% from steady state");
    int kx = cx + 180;
    kx = ui_key(kx, 86, UI_S1, false, "Output");
    kx = ui_key(kx, 86, UI_S3, false, "Consumption");
    ui_key(kx, 86, UI_S2, false, "Investment");
    ui_plot_line(&p, xs + (NQ - filled), hist[I] + (NQ - filled), filled, UI_S2);
    ui_plot_line(&p, xs + (NQ - filled), hist[C] + (NQ - filled), filled, UI_S3);
    ui_plot_line(&p, xs + (NQ - filled), hist[Y] + (NQ - filled), filled, UI_S1);

    UiPlot q = ui_plot(cx, 334, cw, 110, NULL, -(NQ - 1), 0, -r2, r2, "%.0f", "%.0f");
    ui_text(cx, 308, UI_BODY, UI_INK, "% from steady state");
    kx = ui_key(cx + 180, 308, UI_INK, false, "Hours worked");
    ui_key(kx, 308, UI_MUTED, false, "Technology");
    ui_plot_line(&q, xs + (NQ - filled), hist[A] + (NQ - filled), filled, UI_MUTED);
    ui_plot_line(&q, xs + (NQ - filled), hist[N] + (NQ - filled), filled, UI_INK);
    ui_text_right(cx + cw, 334 + 110 + 20, UI_SMALL, UI_MUTED, "quarters ago");

    if (broken) {
        const char *msg = "COZY REPORTED AN ERROR";
        int w = ui_text_w(macro_error, UI_BODY) + 32, bx = cx + (cw - w) / 2, by = 170;
        if (w < ui_text_w(msg, UI_BIG) + 32) w = ui_text_w(msg, UI_BIG) + 32;
        ui_rect(bx, by, w, 76, UI_BG);
        ui_frame_rect(bx, by, w, 76, UI_S2);
        ui_text(bx + 16, by + 10, UI_BIG, UI_INK, msg);
        ui_text(bx + 16, by + 50, UI_BODY, UI_INK2, macro_error);
    }

    /* the model itself, exactly as it was handed to Cozy */
    int ey = 490;
    ui_text(16, ey, UI_BODY, UI_INK, "The model, as Cozy reads it"); ey += 22;
    for (int i = 0; i < RBC_NEQ; i++, ey += 18) ui_text(32, ey, UI_BODY, UI_INK2, RBC_EQS[i]);
    ui_textf(16, ey + 4, UI_SMALL, UI_MUTED, "Linearised and solved by the dsge package of Cozy %s, inside this program, in %.2f ms.", cozy_version(), solve_ms);
}

static int app(int argc, char **argv)
{
    if (ui_args(argc, argv, "cycles", "business cycles from technology shocks, solved by Cozy and run live")) return run_tests() ? 1 : 0;
    if (!macro_start() || !rbc_define()) { fprintf(stderr, "cycles: %s\n", macro_error); return 1; }
    econ = TEXTBOOK;
    rng_seed(&rng, 11);
    resolve();
    if (!ui_open("Business cycles, live")) return 1;
    ui_run(draw);
    return 0;
}
UI_MAIN(app)
