/* irf.c — impulse responses: one shock, and what the model says comes next.
 *
 * Three experiments on the two models of models.h: a rise in technology in
 * the real business cycle model, and a monetary tightening and a rise in
 * demand in the New Keynesian model. Every time a slider moves, Cozy's dsge
 * package, running inside this program, solves the model again and traces
 * the forty quarters after the shock (dsge.irf); C draws them, one small
 * plot per variable, against a reference path — the textbook calibration,
 * or any earlier setting you choose to pin.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#include "ui.h"
#include "models.h"

#define T 40                                     /* quarters traced */
#define NP 6                                     /* plots per experiment */
enum { TECH = 0, HIKE, DEMAND, NEXP };

typedef struct { double rho, sigma, phi, delta; } RbcPar;                 /* delta in percent */
typedef struct { double phi_pi, phi_x, kappa, sigma, rho_n, rho_v; } NkPar;
static const RbcPar RBC_TEXTBOOK = {0.95, 1.0, 1.0, 2.5};
static const NkPar NK_TEXTBOOK = {1.5, 0.125, 0.1275, 1.0, 0.9, 0.5};

typedef struct {
    bool determinate;                            /* false: the rule admits many paths, none drawn */
    double r[NP][T];                             /* the six plotted paths */
} Resp;

static const char *const TITLES[NEXP][NP] = {
    {"Output, %", "Consumption, %", "Investment, %", "Hours, %", "Capital, %", "Technology, %"},
    {"Output gap, %", "Inflation, % a year", "Policy rate, % a year", "Real rate, % a year", "Policy shock, % a year", "Price level, %"},
    {"Output gap, %", "Inflation, % a year", "Policy rate, % a year", "Real rate, % a year", "Natural rate, % a year", "Price level, %"},
};
static const char *const SHOCKS[NEXP] = {
    "Technology rises 1% and fades.",
    "The rule is tightened by 1 point a year.",
    "The natural rate rises 1 point a year.",
};

/* Cozy source: trace `shock` through the solution SOL for T quarters, as a
 * T x 6 matrix R_ whose columns follow the model's variables. */
#define TRACE(SOL, SHOCK) \
    "let r_ = dsge.irf(" SOL ", \"" SHOCK "\", 40); let v_ = " SOL ".model.vars; let R_ = zeros(40, 6);" \
    "for j_ = 1 : 6 do R_[:, j_] = getfield(r_, v_[j_]) end;"

static double raw[T * 6];                        /* R_ as Cozy returned it, row-major */

/* A 1% rise in technology in the business cycle model, in percent. */
static bool respond_rbc(const RbcPar *p, Resp *out)
{
    if (!macro_run(RBC_SOLVE TRACE("t_", "e"), p->rho, p->sigma, p->phi, p->delta / 100, 1.0)) return false;
    if (!macro_read("R_", raw, T, 6)) return false;
    out->determinate = true;
    for (int t = 0; t < T; t++) for (int v = 0; v < NP; v++) out->r[v][t] = raw[t * 6 + v];
    return true;
}

/* A shock of a quarter point (one point at an annual rate) in the New
 * Keynesian model: to the rule (HIKE) or to the natural rate (DEMAND).
 * Rates are shown at annual rates; the price level cumulates inflation. */
static bool respond_nk(const NkPar *p, int which, Resp *out)
{
    char over[256];
    double u;
    snprintf(over, sizeof over, "{phi_pi = %.6f, phi_x = %.6f, kappa = %.6f, sigma = %.6f, rho_n = %.6f, rho_v = %.6f}",
             p->phi_pi, p->phi_x, p->kappa, p->sigma, p->rho_n, p->rho_v);
    if (!macro_run("let b_ = dsge.bk(nk, %s); let u_ = b_.unstable;", over) || !cozy_scalar("u_", &u)) return false;
    out->determinate = (int)u == 2;
    if (!out->determinate) return true;
    if (!macro_run(which == HIKE ? "let s_ = dsge.solve(nk, %s); let s_ = setfield(s_, \"sd\", [0.25; 0.25]);" TRACE("s_", "ev")
                                 : "let s_ = dsge.solve(nk, %s); let s_ = setfield(s_, \"sd\", [0.25; 0.25]);" TRACE("s_", "en"), over)) return false;
    if (!macro_read("R_", raw, T, 6)) return false;
    double level = 0;
    for (int t = 0; t < T; t++) {
        const double *row = raw + t * 6;
        level += row[NK_PI];
        out->r[0][t] = row[NK_X];
        out->r[1][t] = 4 * row[NK_PI];
        out->r[2][t] = 4 * row[NK_I];
        out->r[3][t] = 4 * row[NK_R];
        out->r[4][t] = 4 * row[which == HIKE ? NK_V : NK_RN];
        out->r[5][t] = level;
    }
    return true;
}

/* Quarters from the peak until the response is half the peak; -1 if it is
 * not within the horizon, or the response is nil. */
static int half_life(const double *x)
{
    int peak = 0;
    for (int t = 1; t < T; t++) if (fabs(x[t]) > fabs(x[peak])) peak = t;
    if (fabs(x[peak]) < 1e-12) return -1;
    for (int t = peak + 1; t < T; t++) if (fabs(x[t]) <= 0.5 * fabs(x[peak])) return t - peak;
    return -1;
}

/* ---- model checks (--test) ------------------------------------------- */
static int run_tests(void)
{
    int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { fails++; printf("irf FAIL: %s\n", what); } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)
    Resp a, b;
    CHECK(macro_start() && rbc_define() && nk_define(), "Cozy starts, loads the dsge package and compiles both models");

    /* technology */
    RbcPar rp = RBC_TEXTBOOK;
    CHECK(respond_rbc(&rp, &a), "Cozy traces a technology shock");
    double ms = macro_ms;
    bool decay = true;
    for (int t = 0; t < T; t++) if (!NEAR(a.r[RBC_A][t], pow(rp.rho, t))) decay = false;
    CHECK(decay, "technology rises 1% and decays at its persistence");
    double P[36];
    bool recursion = macro_read("P_", P, 6, 6);
    for (int t = 0; recursion && t + 1 < T; t++) for (int i = 0; i < 6; i++) {
        double next = 0;
        for (int j = 0; j < 6; j++) next += P[i * 6 + j] * raw[t * 6 + j];
        if (!NEAR(next, raw[(t + 1) * 6 + i])) recursion = false;
    }
    CHECK(recursion, "the traced path is the decision rule applied forty times");
    CHECK(a.r[RBC_I][0] > a.r[RBC_Y][0] && a.r[RBC_Y][0] > a.r[RBC_C][0] && a.r[RBC_C][0] > 0, "on impact investment rises most, then output, then consumption");
    int kpeak = 0;
    for (int t = 1; t < T; t++) if (a.r[RBC_K][t] > a.r[RBC_K][kpeak]) kpeak = t;
    CHECK(kpeak > 8 && a.r[RBC_K][0] < 0.5 * a.r[RBC_K][kpeak], "capital builds slowly: its response is a hump");
    CHECK(half_life(a.r[RBC_A]) == 14, "at a persistence of 0.95 the half-life of technology is 14 quarters");

    /* a monetary tightening: Gali's closed form */
    NkPar np = NK_TEXTBOOK;
    CHECK(respond_nk(&np, HIKE, &a) && a.determinate, "Cozy traces a monetary tightening");
    double lam = 1 / ((1 - NK_BETA * np.rho_v) * (np.sigma * (1 - np.rho_v) + np.phi_x) + np.kappa * (np.phi_pi - np.rho_v));
    CHECK(NEAR(a.r[0][0], -0.25 * (1 - NK_BETA * np.rho_v) * lam) && NEAR(a.r[1][0], -np.kappa * lam), "the gap and inflation fall by the closed form's amounts");
    bool geometric = true, fisher = true;
    for (int t = 0; t + 1 < T; t++) {
        if (!NEAR(a.r[1][t + 1], np.rho_v * a.r[1][t])) geometric = false;
        if (!NEAR(a.r[3][t], a.r[2][t] - a.r[1][t + 1])) fisher = false;
    }
    CHECK(geometric, "the effects fade at the shock's own persistence");
    CHECK(fisher, "the real rate is the policy rate less next quarter's inflation");
    CHECK(a.r[3][0] > 0 && a.r[2][0] < 1, "the real rate rises, the policy rate by less than the shock");
    CHECK(fabs(a.r[5][T - 1] - a.r[1][0] / 4 / (1 - np.rho_v)) < 1e-9 && a.r[5][T - 1] < 0, "the price level ends permanently lower");
    CHECK(half_life(a.r[1]) == 1, "at a persistence of 0.5 the half-life of the effect is one quarter");

    /* demand */
    CHECK(respond_nk(&np, DEMAND, &a) && a.r[0][0] > 0 && a.r[1][0] > 0 && a.r[2][0] > 0, "more demand raises the gap, inflation and the policy rate");
    CHECK(a.r[3][0] > 0 && a.r[3][0] < a.r[4][0], "the real rate rises by less than the natural rate, so the gap opens");
    NkPar tough = NK_TEXTBOOK; tough.phi_pi = 3;
    CHECK(respond_nk(&tough, DEMAND, &b) && b.r[1][0] < a.r[1][0] && b.r[0][0] < a.r[0][0], "a tougher rule lets less of a demand shock through");
    NkPar weak = NK_TEXTBOOK; weak.phi_pi = 0.9;
    CHECK(respond_nk(&weak, HIKE, &b) && !b.determinate, "a rule with phi_pi = 0.9 has no unique response to trace");
#undef CHECK
#undef NEAR
    if (!fails) printf("irf: 16 model checks passed (Cozy %s solves and traces in %.2f ms)\n", cozy_version(), ms);
    return fails;
}

/* ---- the window ------------------------------------------------------- */
static int which = HIKE;
static RbcPar rbc_par;
static NkPar nk_par;
static Resp now, ref[NEXP];
static bool pinned[NEXP];
static bool broken;
static double solve_ms;

static bool respond(int e, Resp *out) { return e == TECH ? respond_rbc(&rbc_par, out) : respond_nk(&nk_par, e, out); }
static void resolve(void)
{
    broken = !respond(which, &now);
    solve_ms = macro_ms;
}
static void textbook(void)
{
    rbc_par = RBC_TEXTBOOK; nk_par = NK_TEXTBOOK;
    for (int e = 0; e < NEXP; e++) { respond(e, &ref[e]); pinned[e] = false; }
    resolve();
}

static void small_plot(int x, int y, int w, int h, const char *title, const double *cur, const double *base, bool xticks)
{
    static double xs[T];
    double lo = 0, hi = 0;
    for (int t = 0; t < T; t++) {
        xs[t] = t;
        if (base[t] < lo) lo = base[t];
        if (base[t] > hi) hi = base[t];
        if (cur && cur[t] < lo) lo = cur[t];
        if (cur && cur[t] > hi) hi = cur[t];
    }
    double span = hi - lo;
    if (span < 1e-9) { lo = -1; hi = 1; span = 2; }
    lo -= 0.08 * span; hi += 0.08 * span;
    const char *fmt = span >= 20 ? "%.0f" : span >= 2 ? "%.1f" : span >= 0.2 ? "%.2f" : "%.3f";
    UiPlot p = ui_plot(x, y, w, h, title, 0, T - 1, lo, hi, xticks ? "%.0f" : NULL, fmt);
    ui_plot_line(&p, xs, base, T, UI_MUTED);
    if (cur) ui_plot_line(&p, xs, cur, T, UI_S1);
}

static void draw(void)
{
    const int sx = UI_PANEL_X, sw = UI_PANEL_W;
    static const char *const EXPS[NEXP] = {"Technology", "Rate hike", "Demand"};
    char a[32], b[64];
    ui_header("Impulse responses", "One shock at quarter 0, and what the model says comes next. Cozy solves it again as you drag.");

    /* which experiment */
    int yy = 84;
    bool moved = false;
    for (int e = 0; e < NEXP; e++) {
        if (ui_button(sx + e * 127, yy, 118, EXPS[e]) && e != which) { which = e; moved = true; }
        if (e == which) ui_rect(sx + e * 127, yy + 30, 118, 3, UI_S1);
    }
    yy += 44;
    ui_text(sx, yy, UI_BODY, UI_INK, SHOCKS[which]); yy += 30;

    if (which == TECH) {
        moved |= ui_slider(sx, yy, sw, "Persistence of technology", &rbc_par.rho, 0.0, 0.99, 0.01, "%.2f"); yy += 46;
        moved |= ui_slider(sx, yy, sw, "Risk aversion", &rbc_par.sigma, 0.5, 6.0, 0.1, "%.1f"); yy += 46;
        moved |= ui_slider(sx, yy, sw, "Reluctance to vary hours", &rbc_par.phi, 0.0, 10.0, 0.25, "%.2f"); yy += 46;
        moved |= ui_slider(sx, yy, sw, "Depreciation, % a quarter", &rbc_par.delta, 1.0, 10.0, 0.25, "%.2f"); yy += 46;
        yy += 46;
    } else {
        moved |= ui_slider(sx, yy, sw, "Rule: response to inflation", &nk_par.phi_pi, 0.0, 3.0, 0.05, "%.2f"); yy += 46;
        moved |= ui_slider(sx, yy, sw, "Rule: response to the output gap", &nk_par.phi_x, 0.0, 1.0, 0.025, "%.3f"); yy += 46;
        moved |= ui_slider(sx, yy, sw, "Slope of the Phillips curve", &nk_par.kappa, 0.02, 0.5, 0.0025, "%.4f"); yy += 46;
        moved |= ui_slider(sx, yy, sw, "Persistence of the shock", which == HIKE ? &nk_par.rho_v : &nk_par.rho_n, 0.0, 0.95, 0.05, "%.2f"); yy += 46;
        moved |= ui_slider(sx, yy, sw, "Risk aversion", &nk_par.sigma, 0.5, 6.0, 0.1, "%.1f"); yy += 46;
    }
    yy += 4;
    if (moved) resolve();
    bool drawn = !broken && now.determinate;
    if (ui_button(sx, yy, 181, "Pin as reference") && drawn) { ref[which] = now; pinned[which] = true; }
    if (ui_button(sx + 191, yy, 181, "Textbook values") || ui.reset) textbook();
    yy += 42;
    int kx = ui_key(sx, yy, UI_S1, false, "these settings");
    ui_key(kx, yy, UI_MUTED, false, pinned[which] ? "pinned" : "textbook");
    yy += 30;

    /* the first plot's variable, in two numbers */
    int tw = sw / 2 - 6;
    const char *what = which == TECH ? "Output" : "Output gap";
    drawn = !broken && now.determinate;
    if (drawn) snprintf(a, sizeof a, "%+.2f%%", now.r[0][0]); else snprintf(a, sizeof a, "-");
    snprintf(b, sizeof b, "%s on impact", what);
    ui_tile(sx, yy, tw, b, a, ref[which].determinate ? "" : NULL);
    if (ref[which].determinate) ui_textf(sx + 10, yy + 60, UI_SMALL, UI_MUTED, "reference: %+.2f%%", ref[which].r[0][0]);
    int hl = drawn ? half_life(now.r[0]) : -1;
    if (hl > 0) snprintf(a, sizeof a, "%d qtr%s", hl, hl == 1 ? "" : "s"); else snprintf(a, sizeof a, drawn ? "> %d" : "-", T);
    ui_tile(sx + tw + 12, yy, tw, "Half gone after", a, "from its peak");
    yy += 88;
    ui_textf(sx, yy, UI_SMALL, UI_MUTED, "Solved and traced by Cozy %s in %.2f ms.", cozy_version(), solve_ms);

    /* six small plots, each on its own scale */
    static const int PX[3] = {60, 318, 576}, PY[2] = {112, 312};
    for (int k = 0; k < NP; k++)
        small_plot(PX[k % 3], PY[k / 3], 190, 130, TITLES[which][k], drawn ? now.r[k] : NULL, ref[which].r[k], k >= 3);
    ui_text_right(PX[2] + 190, PY[1] + 130 + 20, UI_SMALL, UI_MUTED, "quarters after the shock");

    if (!drawn) {
        const char *msg = broken ? "COZY REPORTED AN ERROR" : "NO UNIQUE EQUILIBRIUM UNDER THIS RULE";
        const char *sub = broken ? macro_error : "Many paths satisfy the model. Raise the response to inflation.";
        int w = ui_text_w(msg, UI_BIG) + 32, bx = 16 + (760 - w) / 2, by = 236;
        ui_rect(bx, by, w, 76, UI_BG);
        ui_frame_rect(bx, by, w, 76, UI_S2);
        ui_text(bx + 16, by + 10, UI_BIG, UI_INK, msg);
        ui_text(bx + 16, by + 50, UI_BODY, UI_INK2, sub);
    }

    /* the model itself, exactly as it was handed to Cozy */
    int ey = 496;
    const char *const *eqs = which == TECH ? RBC_EQS : NK_EQS;
    ui_text(16, ey, UI_BODY, UI_INK, which == TECH ? "The business cycle model, as Cozy reads it" : "The New Keynesian model, as Cozy reads it"); ey += 22;
    for (int i = 0; i < 6; i++, ey += 18) ui_text(32, ey, UI_BODY, UI_INK2, eqs[i]);
}

static int app(int argc, char **argv)
{
    if (ui_args(argc, argv, "irf", "impulse responses of two macro models, solved by Cozy as you drag")) return run_tests() ? 1 : 0;
    if (!macro_start() || !rbc_define() || !nk_define()) { fprintf(stderr, "irf: %s\n", macro_error); return 1; }
    textbook();
    if (!ui_open("Impulse responses")) return 1;
    ui_run(draw);
    return 0;
}
UI_MAIN(app)
