/* nk.c — a New Keynesian economy, running live.
 *
 * The three-equation model (Gali 2015, ch. 3): a Phillips curve, an IS
 * curve and an interest-rate rule, with a persistent demand disturbance
 * (the natural rate of interest) and a monetary policy disturbance. The
 * model below is text; Cozy's dsge package, running inside this program,
 * solves it for the decision rule y = P y(-1) + Q e every time a slider
 * moves. C then steps the economy one quarter at a time, drawing shocks, and
 * plots the output gap, inflation and the policy rate as they go by.
 *
 * Push the inflation response of the rule below one and the model loses its
 * unique equilibrium: Cozy reports it, and the economy stops.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#include "ui.h"
#include "rng.h"
#include "models.h"

/* ---- the model: its text is in models.h ------------------------------ */
enum { X = NK_X, PI = NK_PI, RATE = NK_I, RN = NK_RN, V = NK_V, NV = NK_NV };
enum { EN = NK_EN, EV = NK_EV, NS = NK_NS };

typedef struct { double phi_pi, phi_x, kappa, sigma, rho_n, rho_v, sd_n, sd_v; } Rule;
static const Rule TEXTBOOK = {1.5, 0.125, 0.1275, 1.0, 0.9, 0.5, 0.25, 0.25};

typedef struct {
    double P[NV * NV], Q[NV * NS];              /* y = P y(-1) + Q e, row-major */
    double sd[NV];                              /* theoretical standard deviations */
    int unstable;                               /* unstable roots; 2 are needed */
    bool determinate;
} Solution;

/* Ask Cozy to solve the model under a rule. A rule with no unique stable
 * equilibrium is not an error: the solution says so. */
static bool model_solve(const Rule *r, Solution *s)
{
    char over[256];
    double u;
    snprintf(over, sizeof over, "{phi_pi = %.6f, phi_x = %.6f, kappa = %.6f, sigma = %.6f, rho_n = %.6f, rho_v = %.6f}",
             r->phi_pi, r->phi_x, r->kappa, r->sigma, r->rho_n, r->rho_v);
    if (!macro_run("let b_ = dsge.bk(nk, %s); let u_ = b_.unstable;", over) || !cozy_scalar("u_", &u)) return false;
    s->unstable = (int)u;
    s->determinate = s->unstable == 2;
    if (!s->determinate) return true;
    if (!macro_run("let s_ = dsge.solve(nk, %s); let s_ = setfield(s_, \"sd\", [%.6f; %.6f]);"
                   "let P_ = s_.P; let Q_ = s_.Q; let sd_ = dsge.moments(s_).sd;", over, r->sd_n, r->sd_v)) return false;
    return macro_read("P_", s->P, NV, NV) && macro_read("Q_", s->Q, NV, NS) && macro_read("sd_", s->sd, NV, 1);
}

/* One quarter: y = P y + Q e. */
static void economy_step(const Solution *s, double *y, const double *e)
{
    double next[NV];
    for (int i = 0; i < NV; i++) {
        next[i] = 0;
        for (int j = 0; j < NV; j++) next[i] += s->P[i * NV + j] * y[j];
        for (int j = 0; j < NS; j++) next[i] += s->Q[i * NS + j] * e[j];
    }
    memcpy(y, next, sizeof next);
}

/* ---- model checks (--test) ------------------------------------------- */
static int run_tests(void)
{
    int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { fails++; printf("nk FAIL: %s\n", what); } } while (0)
    Solution s;
    Rule r = TEXTBOOK;
    CHECK(macro_start(), "Cozy starts and loads the dsge package carried in the program");
    CHECK(nk_define(), "Cozy compiles the model text");
    CHECK(model_solve(&r, &s) && s.determinate, "the textbook rule has a unique stable equilibrium");
    double ms = macro_ms;

    /* what came back from Cozy is Gali's closed form */
    double lam = 1 / ((1 - NK_BETA * r.rho_v) * (r.sigma * (1 - r.rho_v) + r.phi_x) + r.kappa * (r.phi_pi - r.rho_v));
    CHECK(fabs(s.Q[X * NS + EV] + (1 - NK_BETA * r.rho_v) * lam) < 1e-10, "the gap's response to a policy shock is the closed form");
    CHECK(fabs(s.Q[PI * NS + EV] + r.kappa * lam) < 1e-10, "inflation's response to a policy shock is the closed form");
    CHECK(fabs(s.P[RN * NV + RN] - r.rho_n) < 1e-12 && fabs(s.P[V * NV + V] - r.rho_v) < 1e-12, "the disturbances keep their persistence");
    CHECK(s.Q[RATE * NS + EV] > 0 && s.Q[X * NS + EV] < 0 && s.Q[PI * NS + EV] < 0, "a tightening raises the rate and lowers the gap and inflation");

    /* the economy stepped in C has the moments Cozy computed */
    Rng rng; rng_seed(&rng, 1);
    double y[NV] = {0}, sum2[NV] = {0};
    const int T = 400000;
    for (int t = 0; t < T; t++) {
        double e[NS] = {r.sd_n * rng_normal(&rng), r.sd_v * rng_normal(&rng)};
        economy_step(&s, y, e);
        for (int i = 0; i < NV; i++) sum2[i] += y[i] * y[i];
    }
    bool moments = true;
    for (int i = 0; i < NV; i++) if (fabs(sqrt(sum2[i] / T) / s.sd[i] - 1) > 0.03) moments = false;
    CHECK(moments, "simulated standard deviations match Cozy's theoretical ones within 3%");

    /* a single policy shock decays at the disturbance's own rate */
    double z[NV] = {0}, e1[NS] = {0, 1}, e0[NS] = {0, 0};
    economy_step(&s, z, e1);
    double first = z[PI];
    economy_step(&s, z, e0);
    CHECK(fabs(z[PI] / first - r.rho_v) < 1e-10, "a policy shock's effect on inflation halves each quarter");

    /* the Taylor principle, from Cozy's Blanchard-Kahn count */
    r.phi_pi = 0.9;
    CHECK(model_solve(&r, &s) && !s.determinate && s.unstable == 1, "a rule with phi_pi = 0.9 is indeterminate: 1 unstable root of 2");
    double edge = 1 - (1 - NK_BETA) * r.phi_x / r.kappa;
    r.phi_pi = edge + 0.01; bool above = model_solve(&r, &s) && s.determinate;
    r.phi_pi = edge - 0.01; bool below = model_solve(&r, &s) && !s.determinate;
    CHECK(above && below, "determinacy switches at kappa (phi_pi - 1) + (1 - beta) phi_x = 0");

    /* a stronger response to inflation stabilises it */
    Rule soft = TEXTBOOK, hard = TEXTBOOK; hard.phi_pi = 3.0;
    Solution a, b;
    CHECK(model_solve(&soft, &a) && model_solve(&hard, &b) && b.sd[PI] < a.sd[PI], "a tougher rule lowers the volatility of inflation");
    CHECK(!macro_run("let oops = dsge.solve(nk, {no_such = 1});") && strstr(macro_error, "not a parameter"), "a Cozy error comes back as a message");
#undef CHECK
    if (!fails) printf("nk: 12 model checks passed (Cozy %s solves the model in %.2f ms)\n", cozy_version(), ms);
    return fails;
}

/* ---- the window ------------------------------------------------------- */
#define NQ 160                                   /* quarters on screen */
static Rule rule;
static Solution sol;
static double y[NV], pending[NS];
static double hist[3][NQ];                        /* the gap, inflation, the rate */
static int filled;
static double p_speed = 12, clock_q, solve_ms;
static Rng rng;
static bool broken;                               /* Cozy reported an error */

static void resolve(void)
{
    broken = !model_solve(&rule, &sol);
    solve_ms = macro_ms;
}
static void clear_history(void)
{
    memset(y, 0, sizeof y); memset(hist, 0, sizeof hist); memset(pending, 0, sizeof pending);
    filled = 0;
}

static void strip(int x, int yy, int w, int h, const char *title, const double *v, double range, UiColor c, bool xticks)
{
    UiPlot p = ui_plot(x, yy, w, h, title, -(NQ - 1), 0, -range, range, xticks ? "%.0f" : NULL, "%.1f");
    static double xs[NQ];
    for (int i = 0; i < NQ; i++) xs[i] = i - (NQ - 1);
    ui_plot_line(&p, xs + (NQ - filled), v + (NQ - filled), filled, c);
}

static void draw(void)
{
    const int sx = UI_PANEL_X, sw = UI_PANEL_W;
    char a[32], b[64];
    ui_header("A New Keynesian economy, live", "The model is six lines of text. Cozy solves it; the economy runs on the answer.");

    int yy = 84;
    bool moved = false;
    moved |= ui_slider(sx, yy, sw, "Rule: response to inflation", &rule.phi_pi, 0.0, 3.0, 0.05, "%.2f"); yy += 46;
    moved |= ui_slider(sx, yy, sw, "Rule: response to the output gap", &rule.phi_x, 0.0, 1.0, 0.025, "%.3f"); yy += 46;
    moved |= ui_slider(sx, yy, sw, "Slope of the Phillips curve", &rule.kappa, 0.02, 0.5, 0.0025, "%.4f"); yy += 46;
    moved |= ui_slider(sx, yy, sw, "Persistence of demand shocks", &rule.rho_n, 0.0, 0.95, 0.05, "%.2f"); yy += 46;
    moved |= ui_slider(sx, yy, sw, "Size of demand shocks", &rule.sd_n, 0.0, 1.0, 0.05, "%.2f"); yy += 46;
    moved |= ui_slider(sx, yy, sw, "Size of policy shocks", &rule.sd_v, 0.0, 1.0, 0.05, "%.2f"); yy += 46;
    ui_slider(sx, yy, sw, "Quarters per second", &p_speed, 1, 30, 1, "%.0f"); yy += 50;
    if (moved) resolve();
    if (ui_button(sx, yy, 118, "Rate hike")) pending[EV] += 1.0;
    if (ui_button(sx + 127, yy, 118, "Demand up")) pending[EN] += 1.0;
    if (ui_button(sx + 254, yy, 118, "Clear") || ui.reset) clear_history();
    yy += 42;

    /* time: p_speed quarters a second at sixty frames a second */
    if (!ui.paused && sol.determinate && !broken) {
        for (clock_q += p_speed / 60.0; clock_q >= 1.0; clock_q -= 1.0) {
            double e[NS] = {rule.sd_n * rng_normal(&rng) + pending[EN], rule.sd_v * rng_normal(&rng) + pending[EV]};
            pending[EN] = pending[EV] = 0;
            economy_step(&sol, y, e);
            for (int k = 0; k < 3; k++) { memmove(hist[k], hist[k] + 1, (NQ - 1) * sizeof(double)); hist[k][NQ - 1] = y[k]; }
            if (filled < NQ) filled++;
        }
    }

    /* tiles: what Cozy says the rule implies, beside what this run shows */
    int tw = sw / 2 - 6;
    double run_sd[3] = {0};
    for (int k = 0; k < 3; k++) {
        for (int i = NQ - filled; i < NQ; i++) run_sd[k] += hist[k][i] * hist[k][i];
        run_sd[k] = filled ? sqrt(run_sd[k] / filled) : 0;
    }
    if (broken) ui_tile(sx, yy, tw, "Equilibrium", "Error", "see below");
    else ui_tile(sx, yy, tw, "Equilibrium", sol.determinate ? "Unique" : "Not unique",
                 sol.determinate ? "2 unstable roots of 2" : sol.unstable < 2 ? "unstable roots < 2" : "unstable roots > 2");
    if (sol.determinate && !broken) snprintf(a, sizeof a, "%.3f", sol.sd[PI] * sol.sd[PI] + 0.5 * sol.sd[X] * sol.sd[X]); else snprintf(a, sizeof a, "-");
    ui_tile(sx + tw + 12, yy, tw, "Central bank's loss", a, "var(pi) + var(x) / 2");
    yy += 88;
    if (sol.determinate && !broken) snprintf(a, sizeof a, "%.2f", sol.sd[PI]); else snprintf(a, sizeof a, "-");
    snprintf(b, sizeof b, "this run: %.2f", run_sd[1]);
    ui_tile(sx, yy, tw, "Inflation volatility", a, b);
    if (sol.determinate && !broken) snprintf(a, sizeof a, "%.2f", sol.sd[X]); else snprintf(a, sizeof a, "-");
    snprintf(b, sizeof b, "this run: %.2f", run_sd[0]);
    ui_tile(sx + tw + 12, yy, tw, "Gap volatility", a, b);
    yy += 88;
    ui_text(sx, yy, UI_SMALL, UI_MUTED, "Large figures: what the model implies (Cozy).");

    /* the three series, on one scale so they can be compared */
    static double range = 2.0;
    double want = 1.0;
    for (int k = 0; k < 3; k++) {
        double m = sol.determinate ? 3.2 * sol.sd[k] : 0;
        for (int i = NQ - filled; i < NQ; i++) if (fabs(hist[k][i]) * 1.1 > m) m = fabs(hist[k][i]) * 1.1;
        if (m > want) want = m;
    }
    range += (want - range) * 0.05;
    const int cx = 64, cw = 700, ch = 104;
    strip(cx, 108, cw, ch, "Output gap, %", hist[0], range, UI_S1, false);
    strip(cx, 250, cw, ch, "Inflation, % a quarter", hist[1], range, UI_S2, false);
    strip(cx, 392, cw, ch, "Policy rate, % a quarter, from steady state", hist[2], range, UI_S3, true);
    ui_text_right(cx + cw, 392 + ch + 20, UI_SMALL, UI_MUTED, "quarters ago");

    if (!sol.determinate || broken) {
        const char *msg = broken ? "COZY REPORTED AN ERROR" : "NO UNIQUE EQUILIBRIUM UNDER THIS RULE";
        int w = ui_text_w(msg, UI_BIG) + 32, bx = cx + (cw - w) / 2, by = 270;
        ui_rect(bx, by, w, 76, UI_BG);
        ui_frame_rect(bx, by, w, 76, UI_S2);
        ui_text(bx + 16, by + 10, UI_BIG, UI_INK, msg);
        ui_text(bx + 16, by + 50, UI_BODY, UI_INK2, broken ? macro_error : "The economy has stopped. Raise the response to inflation.");
    }

    /* the model itself, exactly as it was handed to Cozy */
    int ey = 536;
    ui_text(16, ey, UI_BODY, UI_INK, "The model, as Cozy reads it"); ey += 22;
    for (int i = 0; i < NK_NEQ; i++, ey += 18) ui_text(32, ey, UI_BODY, UI_INK2, NK_EQS[i]);
    ui_textf(16, ey + 4, UI_SMALL, UI_MUTED, "Solved by the dsge package of Cozy %s, inside this program, in %.2f ms.", cozy_version(), solve_ms);
}

static int app(int argc, char **argv)
{
    if (ui_args(argc, argv, "nk", "a New Keynesian economy, solved by Cozy and run live")) return run_tests() ? 1 : 0;
    if (!macro_start() || !nk_define()) { fprintf(stderr, "nk: %s\n", macro_error); return 1; }
    rule = TEXTBOOK;
    rng_seed(&rng, 7);
    resolve();
    if (!ui_open("A New Keynesian economy, live")) return 1;
    ui_run(draw);
    return 0;
}
UI_MAIN(app)
